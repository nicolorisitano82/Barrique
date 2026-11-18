// Barrique: Wine Mono check. Prints the runtime, the process architecture and a
// small computation through LINQ, collections and string formatting.
using System;
using System.Linq;
using System.Runtime.InteropServices;

class Hello
{
    static int Main()
    {
        int sum = Enumerable.Range(1, 1000).Where(i => i % 3 == 0).Sum();
        Console.WriteLine("dotnet: {0}, {1}, {2}-bit, sum {3}", Environment.Version,
                          RuntimeInformation.ProcessArchitecture, IntPtr.Size * 8, sum);
        return sum == 166833 ? 0 : 1;
    }
}
