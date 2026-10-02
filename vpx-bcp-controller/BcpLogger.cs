using System;
using System.Diagnostics;
using System.IO;
using System.Text;

namespace vpx_bcp_controller
{
    public class BcpLogger
    {
        public string LogFile = "bcplog.txt";
        public bool Enabled = false;
        public bool EchoToConsole = true;
        public bool AddTimeStamp = true;

        private StreamWriter OutputStream;
        private readonly object _sync = new object();
        private bool _closed = false;

        static BcpLogger Singleton = null;

        public static BcpLogger Instance => Singleton;

        public BcpLogger()
        {
            if (Singleton != null)
            {
                Debug.WriteLine("Multiple BcpLogger Singletons exist!");
                return;
            }

            Singleton = this;

            // NOTE: this used to check BcpLogger.Instance.Enabled which is awkward in ctor.
            // If Enabled is false (default), do nothing until EnableLogging() is called.
            if (Enabled)
            {
                OpenStream();
                Write("Init BCP Log");
            }
        }

        // Finalizers + multithreading + IO = pain. Prefer explicit close.
        // If you really want to keep it, at least lock and swallow exceptions.
        //~BcpLogger()
        //{
        //    Close();
        //}

        public void EnableLogging()
        {
            lock (_sync)
            {
                Enabled = true;
                _closed = false;

                CloseStream_NoThrow();
                OpenStream();
            }

            Write("Init BCP Log");
        }

        public void Close()
        {
            lock (_sync)
            {
                _closed = true;
                Enabled = false;
                CloseStream_NoThrow();
            }
        }

        private void OpenStream()
        {
            // lock must already be held
            OutputStream = new StreamWriter(new FileStream(LogFile, FileMode.Create, FileAccess.Write, FileShare.Read))
            {
                AutoFlush = true
            };
        }

        private void CloseStream_NoThrow()
        {
            // lock must already be held
            try { OutputStream?.Flush(); } catch { }
            try { OutputStream?.Dispose(); } catch { }
            OutputStream = null;
        }

        private void Write(string message)
        {
            if (AddTimeStamp)
            {
                DateTime now = DateTime.Now;
                message = string.Format("[{0:H:mm:ss}] {1}", now, message);
            }

            lock (_sync)
            {
                if (_closed || !Enabled || OutputStream == null)
                {
                    // Still optionally echo to Debug even if file is closed
                    if (EchoToConsole) Debug.WriteLine(message);
                    return;
                }

                try
                {
                    OutputStream.WriteLine(message);
                    // AutoFlush=true, no need to Flush() every line
                }
                catch (ObjectDisposedException)
                {
                    // shutting down: ignore
                }
                catch (IOException)
                {
                    // file IO problem: ignore or consider disabling
                }
            }

            if (EchoToConsole)
            {
                Debug.WriteLine(message);
            }
        }

        [Conditional("DEBUG")]
        public static void Trace(string Message)
        {
            var inst = BcpLogger.Instance;
            if (inst == null) return;
            if (!inst.Enabled) return;

            inst.Write(Message);
        }
    }
}