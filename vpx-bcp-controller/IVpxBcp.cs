using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace vpx_bcp_controller
{
    [Guid("f65dfd34-2bad-4a1a-9c6f-630f7a458d27"), ComVisible(true)]
    public interface IVpxBcpController
    {
        #region Properties

        void Connect(int port, string pathToMediaController);
        void Disconnect();
        void Send(string commandMessage);

        [return: MarshalAs(UnmanagedType.Struct, SafeArraySubType = VarEnum.VT_ARRAY)]
        object GetMessages();

        #endregion
    }

    [Guid("4012c448-c67e-4c66-8965-fad543c2a462"), ComVisible(true)]
    public interface IVpxBcpMessage
    {
        #region Properties

        string Command { get; set; }

        string GetValue(string key);

        #endregion
    }
}