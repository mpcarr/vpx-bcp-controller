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

                Thread clientThread = new Thread(new ParameterizedThreadStart(HandleClientCommunications));
                clientThread.Start(_client);
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
            NetworkStream clientStream = tcpClient.GetStream();

            StringBuilder messageBuffer = new StringBuilder(1024);
            byte[] buffer = new byte[1024];
            int bytesRead;

            while (_readerRunning)
            {
                bytesRead = 0;

                try
                {
                    // Blocks until a client sends a message
                    bytesRead = clientStream.Read(buffer, 0, 1024);

                    if (bytesRead > 0)
                    {
                        messageBuffer.Append(Encoding.UTF8.GetString(buffer, 0, bytesRead));

                        // Determine if message is complete (check for message termination character)
                        // If not complete, save the buffer contents and continue to read packets, appending
                        // to saved buffer.  Once completed, convert to a BCP message.
                        int terminationCharacterPos = 0;
                        while ((terminationCharacterPos = messageBuffer.ToString().IndexOf("\n")) > -1)
                        {
                            BcpLogger.Trace("BcpServer: >>>>>>>>>>>>>> Received raw message: " + messageBuffer.ToString(0, terminationCharacterPos + 1));

                            // Convert received data to a BcpMessage
                            BcpMessage message = BcpMessage.CreateFromRawMessage(messageBuffer.ToString(0, terminationCharacterPos + 1));
                            if (message != null)
                            {
                                BcpLogger.Trace("BcpServer: >>>>>>>>>>>>>> Received \"" + message.Command + "\" message: " + message.ToString());

                                // Add BCP message to the queue to be processed
                                BcpMessageManager.Instance.AddMessageToQueue(message);
                            }

                            // Remove the converted message from the buffer
                            messageBuffer.Remove(0, terminationCharacterPos + 1);
                        }
                    }
                    else
                    {
                        // The client has disconnected from the server
                        _readerRunning = false;
                    }
                }
                catch (Exception e)
                {
                    // A socket error has occurred
                    BcpLogger.Trace("BcpServer: Client reader thread exception: " + e.ToString());
                    _readerRunning = false;
                }
            }

            BcpLogger.Trace("BcpServer: HandleClientCommunications thread finish");
            BcpLogger.Trace("BcpServer: Closing TCP/Socket client");
            tcpClient.Close();
        }

        public void Close()
        {
            BcpLogger.Trace("BcpServer: Close start");

            try
            {
                _connectedToServer = false;

                if (ClientConnected)
                {
                    // Send goodbye message to connected client
                    Send(new BcpMessage("goodbye"));
                    _client.Close();
                }
            }
            catch
            {
            }

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
