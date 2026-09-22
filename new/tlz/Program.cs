// global
string prg = "tinylz 2.0b (c) 2024 by zara6502";
List<string> listfiles_temp = new List<string>();

Console.WriteLine(prg);
//Console.ReadLine();

if (args.Length > 0)
{
    listfiles_temp = FillFilesList(args);
    switch (listfiles_temp.Count)
    {
        case 0:
            Console.ForegroundColor = ConsoleColor.Magenta;
            Console.WriteLine("No files to compress.\n");
            PrintUsageStrings();
            break;
        case 1:
            Console.WriteLine("Encode file {0}", listfiles_temp[0]);
            TinyLZ2 tlz2 = new TinyLZ2(File.ReadAllBytes(listfiles_temp[0]), prg.Length);
            break;
    }
}
List<string> FillFilesList(IEnumerable<string> args)
{
    List<string> list = new List<string>();
    foreach (var ar in args)
        if (ar.Contains("*") || ar.Contains("?"))
            list.AddRange(Directory.EnumerateFiles(Path.GetDirectoryName(ar) == "" ? Directory.GetCurrentDirectory() : Path.GetDirectoryName(ar), Path.GetFileName(ar)));
        else list.Add(ar);
    return list;
}
void PrintUsageStrings()
{
    Console.WriteLine("Usage:  tinylz2 [filename1] [filename2] [*.*] - filenames or masks separated by space\n");
}
