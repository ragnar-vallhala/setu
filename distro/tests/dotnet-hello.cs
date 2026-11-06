// SPDX-License-Identifier: MIT OR Apache-2.0
// A tiny .NET Framework program for the smoke tests (lib.sh check_dotnet):
// compiled in the guest with Wine Mono's mcs.exe, then run under the
// personality. It prints the runtime version and uses a few framework
// assemblies (System.Core LINQ, System.Xml) so a broken class library
// shows up, not only a missing runtime.
using System;
using System.Linq;
using System.Xml;

class DotnetHello
{
    static int Main()
    {
        var doc = new XmlDocument();
        doc.LoadXml("<setu><n>1</n><n>2</n><n>3</n></setu>");
        int sum = doc.SelectNodes("/setu/n").Cast<XmlNode>().Sum(n => int.Parse(n.InnerText));
        Console.WriteLine("dotnet-hello: ok, CLR {0}, sum {1}", Environment.Version, sum);
        return sum == 6 ? 0 : 1;
    }
}
