using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Runtime.InteropServices;
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

            if (File.Exists(pathToMediaController))
            {
                Process process = new Process();
                process.StartInfo.FileName = pathToMediaController;
                process.Start();
            }
            else
            {
               BcpLogger.Trace("Executable not found: " + pathToMediaController);
            }

            bcpClient = new BcpServer(port);

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

        private Dictionary<string,string> _parameters { get; set; }

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
            if(_parameters != null && _parameters.ContainsKey(key))
                return _parameters[key];
            return string.Empty;
        }
    }
}
