using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Xml.Linq;

namespace vpx_bcp_controller
{

    [Guid("2ecca4a7-3540-4bf9-8ce2-8bf650e64277"), ComVisible(true), ClassInterface(ClassInterfaceType.None)]
    public class VpxBcpController : IVpxBcpController
    {
        private BcpServer bcpClient;
        private Task sendTask;
        private CancellationTokenSource cancellationTokenSource;
        private readonly ConcurrentQueue<BcpMessage> messageQueue = new ConcurrentQueue<BcpMessage>();

        public VpxBcpController()
        {
            BcpLogger logger = new BcpLogger();
        }

        public async void Connect(int port, string pathToMediaController)
        {
            this.cancellationTokenSource = new CancellationTokenSource();
            BcpLogger.Trace("Attempting to load program: " + pathToMediaController);
            if (pathToMediaController != null && pathToMediaController!=String.Empty)
            {
                string fullPath = Path.GetFullPath(pathToMediaController);
                BcpLogger.Trace("Starting Monitor with full path: " + fullPath);

                if (File.Exists(fullPath))
                {
                    Process process = new Process();
                    process.StartInfo.FileName = fullPath;
                    process.Start();
                }
                else
                {
                    BcpLogger.Trace("Executable not found: " + fullPath);
                    BcpLogger.Trace("Trying Shortcut: " + fullPath + ".lnk");
                    fullPath = fullPath + ".lnk";
                    if (File.Exists(fullPath))
                    {
                        Process process = new Process();
                        process.StartInfo.FileName = fullPath;
                        process.Start();
                    }
                    else
                    {
                        BcpLogger.Trace("Executable not found: " + fullPath);
                    }
                }
            }

            BcpLogger.Trace("Connecting to BCPServer on port: " + port);
            bcpClient = new BcpServer(port);

            if (bcpClient.ClientConnected)
            {
                // Start an asynchronous task to send messages to the server
                sendTask = Task.Run(async () =>
                {
                    while (!this.cancellationTokenSource.Token.IsCancellationRequested)
                    {
                        if (messageQueue.TryDequeue(out BcpMessage message))
                        {
                            BcpServer.Instance.Send(message);
                        }
                    }
                }, this.cancellationTokenSource.Token);


                await sendTask;
            }
            else
            {
                bcpClient.Close();
            }
        }

        public void EnableLogging()
        {
            BcpLogger.Instance.Enabled = true;
        }

        public void Send(string commandMessage)
        {
            try
            {
                BcpMessage message = new BcpMessage();
                message.RawMessage = commandMessage + "\n";
                messageQueue.Enqueue(message);
            }
            catch (Exception ex)
            {
                BcpLogger.Trace(ex.Message);
            }
        }
        public object GetMessages()
        {
            List<BcpMessage> messges = BcpMessageManager.Instance.Update();
            object[] retMessages = new object[messges.Count];
            for(var i=0; i<messges.Count;i++)            
            {
                retMessages[i] = new VpxBcpMessage(messges[i].Command, messges[i].Parameters);
            }
            return retMessages;
        }


        public void Disconnect()
        {
            if (bcpClient != null)
            {
                this.cancellationTokenSource.Cancel();
                BcpServer.Instance.Close();
            }
        }

    }

    [Guid("9c7615bd-db4b-4e2d-a8dd-4184de198c5f"), ComVisible(true)]
    public class VpxBcpMessage : IVpxBcpMessage
    {
        public string Command { get; set; } = string.Empty;

        private Dictionary<string, string> _parameters { get; set; }

        public VpxBcpMessage(string command, Dictionary<string, string> parameters)
        {
            this.Command = command;
            if (parameters != null)
            {
                _parameters = parameters;
            }
        }

        public string GetValue(string key)
        {
            if (_parameters != null && _parameters.ContainsKey(key))
                return _parameters[key];
            return string.Empty;
        }

        [return: MarshalAs(UnmanagedType.Struct, SafeArraySubType = VarEnum.VT_ARRAY)]
        public object GetArrayValue(string key)
        {
            BcpLogger.Trace("Getting: " + key);
            if (_parameters != null && _parameters.ContainsKey("json") && !string.IsNullOrEmpty(_parameters["json"]))
            {
                BcpLogger.Trace("Getting1: " + key);
                try
                {
                    JObject json = JObject.Parse(_parameters["json"]);
                    JToken valueToken = json.SelectToken(key);
                    BcpLogger.Trace("Getting1: " + valueToken.ToString());
                    if (valueToken != null)
                    {
                        return valueToken.ToArray();
                    }
                }
                catch (JsonReaderException)
                {
                    BcpLogger.Trace("Invalid JSON format.");
                }
            }
            return null;

        }
    }
}
