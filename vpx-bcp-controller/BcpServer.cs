using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Sockets;
using System.Net;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using System.Threading;

namespace vpx_bcp_controller
{
    public class BcpServer
    {
        public const string CONTROLLER_VERSION = "0.1.0";
        
        public const string CONTROLLER_NAME = "VPX Pin Controller";

        public const string BCP_SPECIFICATION_VERSION = "1.1";

        private TcpClient _client;
        private Thread _clientThread;

        private volatile int _port;

        private volatile bool _connectedToServer;

        private volatile bool _readerRunning;

        public static BcpServer Instance { get; private set; }

        public bool ClientConnected
        {
            get
            {
                return (_client != null && _client.Connected);
            }
        }

        public BcpServer(int port)
        {
            Instance = this;
            _connectedToServer = false;
            _port = port;
            BcpMessageManager messageManager = new BcpMessageManager();
        }

        public void Init()
        {
            BcpLogger.Trace("BcpServer: Initializing");
            BcpLogger.Trace("BcpServer: " + CONTROLLER_NAME + " " + CONTROLLER_VERSION);
            BcpLogger.Trace("BcpServer: BCP Specification Version " + BCP_SPECIFICATION_VERSION);

            int retryCount = 0;

            while (!_connectedToServer && retryCount < 3)
            {
                try
                {
                    _client = new TcpClient("localhost", _port);
                    _connectedToServer = true; // Set to true if connection is successful
                }
                catch (SocketException ex)
                {
                    BcpLogger.Trace("BcpServer: Failed to connect, retrying...");
                    Thread.Sleep(5000); // Wait for 5 seconds before retrying
                    retryCount++;
                }
            }


            if (_connectedToServer)
            {
                Send(new BcpMessage("hello?version=21&controller_name=VPX&controller_version=0.1.0"));

                _clientThread = new Thread(HandleClientCommunications);
                _clientThread.IsBackground = true;
                _clientThread.Start(_client);
            }

        }

        ~BcpServer()
        {
            Close();
        }

        private void HandleClientCommunications(object client)
        {
            BcpLogger.Trace("BcpServer: HandleClientCommunications thread start");
            _readerRunning = true;

            TcpClient tcpClient = (TcpClient)client;
            NetworkStream clientStream = null;

            // Accumulate one line (one message) at a time
            StringBuilder lineBuffer = new StringBuilder(1024);

            // Read bytes, decode into chars incrementally (handles UTF-8 split chars across packets)
            byte[] byteBuffer = new byte[1024];
            char[] charBuffer = new char[1024]; // 1024 chars is fine; decoder will output <= input size for UTF-8
            Decoder decoder = Encoding.UTF8.GetDecoder();

            try
            {
                clientStream = tcpClient.GetStream();

                while (_readerRunning)
                {
                    int bytesRead;

                    try
                    {
                        // Blocks until bytes arrive or the connection is closed
                        bytesRead = clientStream.Read(byteBuffer, 0, byteBuffer.Length);
                    }
                    catch (IOException) when (!_readerRunning || !_connectedToServer)
                    {
                        // Expected during shutdown: socket closed while a blocking Read() was in progress
                        break;
                    }
                    catch (ObjectDisposedException) when (!_readerRunning || !_connectedToServer)
                    {
                        // Expected during shutdown
                        break;
                    }

                    if (bytesRead <= 0)
                    {
                        // Client disconnected cleanly
                        _readerRunning = false;
                        break;
                    }

                    int charsDecoded = decoder.GetChars(byteBuffer, 0, bytesRead, charBuffer, 0, flush: false);

                    for (int i = 0; i < charsDecoded; i++)
                    {
                        char c = charBuffer[i];
                        lineBuffer.Append(c);

                        if (c == '\n')
                        {
                            // Full message received (includes '\n')
                            string rawMessage = lineBuffer.ToString();
                            lineBuffer.Clear();

                            BcpLogger.Trace("BcpServer: >>>>>>>>>>>>>> Received raw message: " + rawMessage);

                            BcpMessage message = BcpMessage.CreateFromRawMessage(rawMessage);
                            if (message != null)
                            {
                                BcpLogger.Trace(
                                    $"BcpServer: >>>>>>>>>>>>>> Received \"{message.Command}\" message: {message}");
                                BcpMessageManager.Instance.AddMessageToQueue(message);
                            }
                        }
                    }
                }
            }
            catch (Exception e)
            {
                // If we're not intentionally shutting down, log it
                if (_readerRunning && _connectedToServer)
                {
                    BcpLogger.Trace("BcpServer: Client reader thread exception: " + e);
                }
            }
            finally
            {
                BcpLogger.Trace("BcpServer: HandleClientCommunications thread finish");
                BcpLogger.Trace("BcpServer: Closing TCP/Socket client");

                try { clientStream?.Close(); } catch { }
                try { tcpClient.Close(); } catch { }
            }
        }

        public void Close()
        {
            BcpLogger.Trace("BcpServer: Close start");

            try
            {
                _connectedToServer = false;
                _readerRunning = false;

                if (ClientConnected)
                {
                    // Send goodbye message to connected client
                    Send(new BcpMessage("goodbye"));

                    // Unblock Read() more cleanly
                    try { _client?.Client?.Shutdown(SocketShutdown.Both); } catch { }

                    _client?.Close();
                }

                // Wait for the reader thread to exit
                try { _clientThread?.Join(1000); } catch { }
                _clientThread = null;
            }
            catch { }

            BcpLogger.Trace("BcpServer: Close finished");
        }

        public bool Send(BcpMessage message)
        {
            if (!ClientConnected)
                return false;

            try
            {
                NetworkStream clientStream = _client.GetStream();
                byte[] packet;
                int length = message.ToPacket(out packet);
                if (length > 0)
                {
                    clientStream.Write(packet, 0, length);
                    clientStream.Flush();
                    BcpLogger.Trace("BcpServer: <<<<<<<<<<<<<< Sending \"" + message.Command + "\" message: " + message.ToString());
                }
            }
            catch (Exception e)
            {
                BcpLogger.Trace("BcpServer: Sending \"" + message.Command + "\" message FAILED: " + e.ToString());
                return false;
            }

            return true;
        }
    }
}
