using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace vpx_bcp_controller
{

    /// <summary>
    /// This singleton class provides a timestamped log file that will only be available in debug builds.
    /// </summary>
    public class BcpLogger
    {

        public string LogFile = "bcplog.txt";

        public bool Enabled = false;

        public bool EchoToConsole = true;

        public bool AddTimeStamp = true;

        private StreamWriter OutputStream;

        static BcpLogger Singleton = null;

        public static BcpLogger Instance
        {
            get { return Singleton; }
        }

        public BcpLogger()
        {

            if (Singleton != null)
            {
                Debug.WriteLine("Multiple BcpLogger Singletons exist!");
                return;
            }

            Singleton = this;

            // Open the log file to append the new log to it.
            if (BcpLogger.Instance.Enabled)
            {
                OutputStream = new StreamWriter(LogFile, false);
                Write("Init BCP Log");
            }
        }

        ~BcpLogger()
        {
            if (OutputStream != null)
            {
                OutputStream.Close();
                OutputStream = null;
            }
        }

        private void Write(string message)
        {
            if (AddTimeStamp)
            {
                DateTime now = DateTime.Now;
                message = string.Format("[{0:H:mm:ss}] {1}", now, message);
            }

            if (OutputStream != null)
            {

                OutputStream.WriteLine(message);
                OutputStream.Flush();
            }

            if (EchoToConsole)
            {
                Debug.WriteLine(message);
            }
        }

        [Conditional("DEBUG")]
        public static void Trace(string Message)
        {
            if (BcpLogger.Instance != null)
                if (BcpLogger.Instance.Enabled)
                    BcpLogger.Instance.Write(Message);
        }


    }

}
