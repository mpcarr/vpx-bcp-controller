using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace vpx_bcp_controller
{
    public class BcpMessageManager
    {
        public const string BCP_VERSION = "1.1";

        public int messageQueueSize = 100;

        public bool ignoreUnknownMessages = true;
        
        private Queue<BcpMessage> _messageQueue = new Queue<BcpMessage>();

        private object _queueLock = new object();

        public static BcpMessageManager Instance { get; private set; }

        public BcpMessageManager()
        {
            if (Instance == null)
                Instance = this;

            Start();
        }

        void Start()
        {
            BcpServer.Instance.Init();   
        }

        ~BcpMessageManager()
        {
            
        }

        public List<BcpMessage> Update()
        {
            BcpMessage currentMessage = null;
            bool checkMessages = true;
            List<BcpMessage> messages = new List<BcpMessage>();
            
            while (checkMessages)
            {
                lock (_queueLock)
                {
                    if (_messageQueue.Count > 0)
                    {
                        currentMessage = _messageQueue.Dequeue();
                    }
                    else
                    {
                        currentMessage = null;
                        checkMessages = false;
                    }
                }

                if (currentMessage != null)
                {
                    try
                    {
                        messages.Add(currentMessage);
                    }
                    catch (Exception ex)
                    {
                        BcpLogger.Trace("An exception occurred while processing '" + currentMessage.Command + "' message (" + currentMessage.RawMessage + "): " + ex.ToString());
                    }
                }
            }
            return messages;

        }

        public void AddMessageToQueue(BcpMessage message)
        {
            lock (_queueLock)
            {
                if (_messageQueue.Count < messageQueueSize)
                {
                    _messageQueue.Enqueue(message);
                }
            }
        }

    }


}
