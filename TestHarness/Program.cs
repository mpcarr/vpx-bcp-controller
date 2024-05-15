using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.Remoting.Contexts;
using System.Text;
using System.Threading.Tasks;
using vpx_bcp_controller;

namespace TestHarness
{
    internal class Program
    {
        static void Main(string[] args)
        {
            VpxBcpController vpxController = new VpxBcpController();
            vpxController.Connect(5050, null);
            vpxController.Send("{\"name\": \"slides_play\", \"settings\": {\"action\": \"play\"} }");
        }
    }
}
