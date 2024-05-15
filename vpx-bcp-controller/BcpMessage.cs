using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace vpx_bcp_controller
{
        public class BcpMessage
    {
        
        public string Id;

        public string Command;

        public string RawMessage;

        public Dictionary<string,string> Parameters;

        public BcpMessage()
        {
            Id = String.Empty;
            Command = String.Empty;
            RawMessage = String.Empty;
            Parameters = null;
        }

        public BcpMessage(string rawMessage)
        {
            Id = String.Empty;
            Command = String.Empty;
            RawMessage = rawMessage;
            Parameters = null;
        }

        public override string ToString()
        {
            return RawMessage;
        }

        public static BcpMessage CreateFromRawMessage(string rawMessage)
        {
            BcpMessage bcpMessage = new BcpMessage();

            // Remove line feed and carriage return characters
            rawMessage = rawMessage.Replace("\n", String.Empty);
            rawMessage = rawMessage.Replace("\r", String.Empty);

            bcpMessage.RawMessage = rawMessage;
            rawMessage = WebUtility.UrlDecode(rawMessage);

            // Message text occurs before the question mark (?)
            if (rawMessage.Contains("?"))
            {
                int messageDelimiterPos = rawMessage.IndexOf('?');

                // BCP commands are not case sensitive so we convert to lower case
                // BCP parameter names are not case sensitive, but parameter values are
                bcpMessage.Command = rawMessage.Substring(0, messageDelimiterPos).Trim().ToLower();
                rawMessage = rawMessage.Substring(messageDelimiterPos + 1);

                string[] parameters = Regex.Split(rawMessage, "&");

                if (parameters.Length > 0)
                {
                    bcpMessage.Parameters = new Dictionary<string, string>();

                    for (var i = 0; i < parameters.Length; i++)
                    {
                        string[] parameterValuePair = Regex.Split(parameters[i], "=");
                        string name = parameterValuePair[0].Trim().ToLower();

                        if (parameterValuePair.Length == 2)
                        {
                            string value = parameterValuePair[1].Trim();
                            bcpMessage.Parameters.Add(name,value);
                        }
                        else
                        {
                            bcpMessage.Parameters.Add(name, "");
                        }
                    }
                }
            }
            else
            {
                // No parameters in the message, the entire message contains just the message text
                bcpMessage.Command = rawMessage.Trim();
            }

            return bcpMessage;
        }

        public int ToPacket(out byte[] packet)
        {
            packet = Encoding.UTF8.GetBytes(this.ToString());
            return packet.Length;
        }
    }
}
