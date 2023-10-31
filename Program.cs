using System;
using System.Collections;
using System.Collections.Generic;
using System.Data;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using static System.Net.Mime.MediaTypeNames;

namespace ent
{
    class Program
    {
        public static List<string> listfiles   = new List<string>();
        public static List<string> listfiles_temp = new List<string>();
        public static int options = 0;
        public static int step = 0;
        public static int scale = 1;
        const int pngbw = 0b000000001; // -pngbw  : only generate PNG black/white files
        const int png   = 0b000000010; // -png    : only generate PNG colored files
        const int png2  = 0b000000100; // -png2   : only generate PNG colored files scale x2
        const int png4  = 0b000001000; // -png4   : only generate PNG colored files scale x4
        const int nopng = 0b001000000; // -png-   : not  generate PNG files
        const int noobx = 0b010000000; // -obx-   : not  generate OBX files
        static void Main(string[] args) {
            Console.ForegroundColor = ConsoleColor.Blue;
            Console.WriteLine("SCR2ASM 0.5b (c) 2023 by zara6502");
            Console.WriteLine("ZXArt SCR files converter\n");
            if (args.Length > 0)
            {
                listfiles_temp = FillFilesList(args);
                switch (listfiles_temp.Count)
                {
                    case 0:
                        Console.ForegroundColor = ConsoleColor.Magenta;
                        Console.WriteLine("No files to convert.\n");
                        PrintUsageStrings();
                        break;
                    case 1:
                        listfiles = listfiles_temp.GetRange(step, listfiles_temp.Count - step);
                        break;
                    default:
                        SetOptions();
                        listfiles = listfiles_temp.GetRange(step, listfiles_temp.Count - step);
                        break;
                }
                File.Delete(Path.GetDirectoryName(args[0]) + "errors.log"); // clear log file
                switch (options)
                {
                    case 0:
                    case > png4:
                        if ((options & noobx) != 0)
                        {
                            FullCreate_PNG_ASM_OBX(); // <--- main program
                        }
                        else
                        {
                            if(File.Exists("mads.exe")) // <--- MADS Assembler needed
                            {
                                FullCreate_PNG_ASM_OBX(); // <--- main program
                            }
                            else
                            {
                                Console.ForegroundColor = ConsoleColor.Magenta;
                                Console.WriteLine("No MADS.EXE detected.");
                            }
                        }
                        break;
                    case < nopng:
                        OnlyOne();
                        break;
                }
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                PrintUsageStrings();
            }
        }
        public static void SetOptions()
        {
            for (int i = 0; (i < listfiles_temp.Count) && (i < 4); i++)
            {
                switch(listfiles_temp[i])
                {
                    case "-pngbw":
                        options |= pngbw;
                        step = i + 1;
                        break;
                    case "-png":
                        options |= png;
                        step = i + 1;
                        break;
                    case "-png2":
                        options |= png2;
                        step = i + 1;
                        scale = 2;
                        break;
                    case "-png4":
                        options |= png4;
                        step = i + 1;
                        scale = 4;
                        break;
                    case "-nopng":
                        options |= nopng;
                        step = i + 1;
                        break;
                    case "-noobx":
                        options |= noobx;
                        step = i + 1;
                        break;
                }
            }
        }
        public static void OnlyOne()
        {
            if ((options & pngbw) != 0)
            {
                CreateOnlyPNGbwextra();
                //CreateOnlyPNGbw();
                return;
            }
            if ((options & png) != 0 || (options & png2) != 0 || (options & png4) != 0)
            {
                CreateOnlyPNGcolored();
            }
        }
        public static void PrintUsageStrings()
        {
            Console.WriteLine("Usage:  scr2asm [-png[-]][obx[-]][asm[-]] [filename1] [*.scr] - filenames or masks separated by space\n");
            Console.WriteLine("        -pngbw  : only generate monochrome PNG files");
            Console.WriteLine("        -png    : only generate color PNG files");
            Console.WriteLine("        -png2   : only generate color PNG files, scale x2");
            Console.WriteLine("        -png4   : only generate color PNG files, scale x4");
            Console.WriteLine("        -noobx  : not  generate OBX files");
            Console.WriteLine("        -nopng  : not  generate PNG files");
        }
        public static void CreateOnlyPNGcolored()
        {
            Stopwatch sw = new Stopwatch(); // total timer
            TimeSpan ts; // total times to HH:MM:SS
            var counter = 1; // worked files counter
            sw.Restart();
            Console.ForegroundColor = ConsoleColor.Yellow;
            StringBuilder sbe = new StringBuilder();

            var num_i = Enumerable.Range(0, listfiles.Count).ToList(); // Parallel.ForEach total files to convert
            Parallel.ForEach(num_i, i => // main cycle range of total files
            {
                System.IO.FileInfo file = new System.IO.FileInfo(listfiles[i]); // open file to convert
                ts = sw.Elapsed;
                Console.WriteLine(" {1:00}%  {2:00}h {3:00}m {4:00}s  {0}   ", Path.GetFileName(listfiles[i]), (int)(counter * 100 / listfiles.Count), ts.Hours, ts.Minutes, ts.Seconds);
                counter++;
                if (file.Length == 6912)
                {
                    byte[] bitmap = new byte[6144];
                    byte[] attributes = new byte[768]; ;
                    using (BinaryReader reader = new BinaryReader(File.Open(listfiles[i], FileMode.Open)))
                    {
                        reader.Read(bitmap, 0, 6144);
                        reader.Read(attributes, 0, 768);
                    }
                    Bitmap img = new Bitmap(256, 192);
                    for (int y = 0; y < 192; y++)
                    {
                        for (int col = 0; col < 32; col++)
                        {
                            int x = col << 3;
                            byte b = bitmap[((y & 0b11000000) << 5) + ((y & 0b00000111) << 8) + ((y & 0b00111000) << 2) + (x >> 3)];
                            byte attr = attributes[((y & 0b11111000) << 2) + (x >> 3)];
                            int val = ((attr >> 6) & 1) == 0 ? 0xcd : 0xff; // bright
                            //int val = ((attr >> 6) & 1) == 0 ? 0xcd / 2 : 0xff / 2; // bright
                            for (int bit = 0; bit < 8; bit++)
                            {
                                bool bitIsSet = ((b >> (7 - bit)) & 1) == 1;
                                int color = bitIsSet ? (attr & 0b0111) : (attr >> 3) & 0b0111; // ink : paper
                                Color rgb = Color.FromArgb(val * ((color >> 1) & 1), val * ((color >> 2) & 1), val * (color & 1));
                                img.SetPixel(x + bit, y, rgb);
                            }
                        }
                    }
                    if (scale > 1)
                        img = new Bitmap(img, new Size(256 * scale, 192 * scale));
                    img.Save(listfiles[i] + ".png");
                    img.Dispose();
                }
                else
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine(" Incorrect file size");
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    sbe.AppendLine(listfiles[i] + " : Incorrect file size (" + file.Length + ")");
                    sbe.AppendLine("--------------------");
                }
            });
            // write log file if buffer not null
            if (sbe.Length > 0)
            {
                System.IO.File.WriteAllText("errors.log", sbe.ToString());
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("\nErrors encountered, see 'errors.log' file.");
            }
            ts = sw.Elapsed;
            // total time
            Console.ForegroundColor = ConsoleColor.White;
            Console.WriteLine("Total time: {0:00}h {1:00}m {2:00}s", ts.Hours, ts.Minutes, ts.Seconds);
            sw.Stop();
        }
        public static void CreateOnlyPNGbwextra()
        {
            Stopwatch sw = new Stopwatch(); // total timer
            TimeSpan ts; // total times to HH:MM:SS
            var counter = 1; // worked files counter
            sw.Restart();
            Console.ForegroundColor = ConsoleColor.Yellow;
            StringBuilder sbe = new StringBuilder();

            var num_i = Enumerable.Range(0, listfiles.Count).ToList(); // Parallel.ForEach total files to convert
            Parallel.ForEach(num_i, i => // main cycle range of total files
            {
                System.IO.FileInfo file = new System.IO.FileInfo(listfiles[i]); // open file to convert
                ts = sw.Elapsed;
                Console.WriteLine(" {1:00}%  {2:00}h {3:00}m {4:00}s  {0}   ", Path.GetFileName(listfiles[i]), (int)(counter * 100 / listfiles.Count), ts.Hours, ts.Minutes, ts.Seconds);
                counter++;
                if (file.Length == 6912)
                {
                    byte[] bitmap = new byte[6144];
                    byte[] png = new byte[6144];
                    byte[] attributes = new byte[768]; ;
                    using (BinaryReader reader = new BinaryReader(File.Open(listfiles[i], FileMode.Open)))
                    {
                        reader.Read(bitmap, 0, 6144);
                        reader.Read(attributes, 0, 768);
                    }
                    for (int y = 0; y < 192; y++)
                    {
                        var num_col = Enumerable.Range(0, 32).ToList(); // Parallel.ForEach total files to convert
                        Parallel.ForEach(num_col, col => // main cycle range of total files
                        //for (int col = 0; col < 32; col++)
                        {
                            byte attr = attributes[((y & 0b11111000) << 2) + col];
                            byte color = bitmap[((y & 0b11000000) << 5) + ((y & 0b00000111) << 8) + ((y & 0b00111000) << 2) + col];
                            switch (attr) // change color variables
                            {
                                case 0:
                                case 64:
                                    color = 0;
                                    break;
                                case 63:
                                case 127:
                                    color = 255;
                                    break;
                                case 56:
                                case 120:
                                    color = (byte)~color;
                                    break;
                            }
                            png[y * 32 + col] = color; // copy byte from SCR picture to PNG byte array
                        });
                    }
                    // *** convert PNG byte array to bitmap
                    Bitmap destination = new Bitmap(256, 192, PixelFormat.Format1bppIndexed);
                    destination.SetResolution(96, 96);
                    BitmapData destinationData = destination.LockBits(new Rectangle(0, 0, destination.Width, destination.Height), ImageLockMode.WriteOnly, PixelFormat.Format1bppIndexed);
                    Marshal.Copy(png, 0, destinationData.Scan0, destinationData.Stride * 192);
                    destination.UnlockBits(destinationData);
                    // *** save bitmap on disk to PNG format
                    destination.Save(listfiles[i] + ".png");
                }
                else
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine(" Incorrect file size");
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    sbe.AppendLine(listfiles[i] + " : Incorrect file size (" + file.Length + ")");
                    sbe.AppendLine("--------------------");
                }
            });
            // write log file if buffer not null
            if (sbe.Length > 0)
            {
                System.IO.File.WriteAllText("errors.log", sbe.ToString());
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("\nErrors encountered, see 'errors.log' file.");
            }
            ts = sw.Elapsed;
            // total time
            Console.ForegroundColor = ConsoleColor.White;
            Console.WriteLine("Total time: {0:00}h {1:00}m {2:00}s", ts.Hours, ts.Minutes, ts.Seconds);
            sw.Stop();
        }
        public static void CreateOnlyPNGbw()
        {
            Stopwatch sw = new Stopwatch(); // total timer
            TimeSpan ts; // total times to HH:MM:SS
            sw.Restart();
            var num_i = Enumerable.Range(0, listfiles.Count).ToList(); // Parallel.ForEach total files to convert
            var counter = 1; // worked files counter
            Console.ForegroundColor = ConsoleColor.Yellow;
            StringBuilder sbe = new StringBuilder(); // string buffer of error.log
            Parallel.ForEach(num_i, i => // main cycle range of total files
            {
                try // exception zone
                {
                    System.IO.FileInfo file = new System.IO.FileInfo(listfiles[i]); // open file to convert
                    byte[] bytes = File.ReadAllBytes(listfiles[i]); // read opened file to BYTE array
                    // *** out text info of current file: percent of work, time, file name
                    ts = sw.Elapsed;
                    Console.WriteLine(" {1:00}%  {2:00}h {3:00}m {4:00}s  {0}   ", Path.GetFileName(listfiles[i]), (int)(counter * 100 / listfiles.Count), ts.Hours, ts.Minutes, ts.Seconds);
                    // ***
                    counter++; // increment of worked file
                    if (bytes.Length >= 6144) // convert file only 6144 bytes or larger
                    {
                        byte[] png = new byte[6144]; // second BYTE array for PNG files creation
                        int png_y = 0; // PNG file Y line position
                        // ZX screen memory format: 3 pages, 8 blocks of 8 lines per page
                        for (var pages = 0; pages < 3; pages++)
                        {
                            for (var eightlines = 0; eightlines < 8; eightlines++)
                            {
                                for (var eightlineblock = 0; eightlineblock < 8; eightlineblock++)
                                {
                                    var num_x = Enumerable.Range(0, 32).ToList(); // Parallel.ForEach total columns (BYTE) to convert in one line
                                    Parallel.ForEach(num_x, x => // cycle range of picture one line (32 bytes)
                                    {
                                        byte color = bytes[(pages * 2048) + (eightlineblock * 256) + eightlines * 32 + x]; // temp variables of color
                                        if (bytes.Length > 6144) // detect attributes
                                        {
                                            int attr_pos = 6144 + x + (png_y >>> 3) * 32; // get position in attributes
                                            switch (bytes[attr_pos]) // change color variables
                                            {
                                                case 0:
                                                case 64:
                                                    color = 0;
                                                    break;
                                                case 63:
                                                case 127:
                                                    color = 255;
                                                    break;
                                                case 56:
                                                case 120:
                                                    color = (byte)~color;
                                                    break;
                                            }
                                        }
                                        png[png_y * 32 + x] = color; // copy byte from SCR picture to PNG byte array
                                    });
                                    png_y++; // change line position of PNG
                                }
                            }
                        }
                        // *** convert PNG byte array to bitmap
                        Bitmap destination = new Bitmap(256, 192, PixelFormat.Format1bppIndexed);
                        destination.SetResolution(96, 96);
                        BitmapData destinationData = destination.LockBits(new Rectangle(0, 0, destination.Width, destination.Height), ImageLockMode.WriteOnly, PixelFormat.Format1bppIndexed);
                        Marshal.Copy(png, 0, destinationData.Scan0, destinationData.Stride * 192);
                        destination.UnlockBits(destinationData);
                        // *** save bitmap on disk to PNG format
                        destination.Save(listfiles[i] + ".png");
                    }
                    else
                    {
                        // exceprion write to log file and console
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine(" Incorrect file size");
                        Console.ForegroundColor = ConsoleColor.Yellow;
                        sbe.AppendLine(listfiles[i]);
                        sbe.AppendLine("Incorrect File Size : <6144 bytes");
                        sbe.AppendLine("--------------------");
                    }
                }
                catch (Exception e)
                {
                    // exceprion write to log file and console
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine(e.Message);
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    sbe.AppendLine(listfiles[i]);
                    sbe.AppendLine(e.Message);
                    sbe.AppendLine("--------------------");
                }
            });
            // write log file if buffer not null
            if (sbe.Length > 0)
            {
                System.IO.File.WriteAllText("errors.log", sbe.ToString());
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("\nErrors encountered, see 'errors.log' file.");
            }
            ts = sw.Elapsed;
            // total time
            Console.ForegroundColor = ConsoleColor.White;
            Console.WriteLine("Total time: {0:00}h {1:00}m {2:00}s", ts.Hours, ts.Minutes, ts.Seconds);
            sw.Stop();
        }
        public static void FullCreate_PNG_ASM_OBX() {
            Stopwatch sw = new Stopwatch(); // total timer
            TimeSpan ts; // total times to HH:MM:SS
            sw.Restart();
            var num_i = Enumerable.Range(0, listfiles.Count).ToList(); // Parallel.ForEach total files to convert
            var counter = 1; // worked files counter
            Console.ForegroundColor = ConsoleColor.Yellow;
            StringBuilder sbe = new StringBuilder(); // string buffer of error.log
            Parallel.ForEach(num_i, i => // main cycle range of total files
            {
                try // exception zone
                {
                    System.IO.FileInfo file = new System.IO.FileInfo(listfiles[i]); // open file to convert
                    byte[] bytes = File.ReadAllBytes(listfiles[i]); // read opened file to BYTE array
                    byte[] hex = { 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46 };
                    // *** out text info of current file: percent of work, time, file name
                    ts = sw.Elapsed;
                    Console.WriteLine(" {1:00}%  {2:00}h {3:00}m {4:00}s  {0}   ", Path.GetFileName(listfiles[i]), (int)(counter * 100 / listfiles.Count), ts.Hours, ts.Minutes, ts.Seconds);
                    // ***
                    counter++; // increment of worked file
                    if (bytes.Length >= 6144) // convert file only 6144 bytes or larger
                    {
                        byte[] png = new byte[6144]; // second BYTE array for PNG files creation
                        int png_y = 0; // PNG file Y line position
                        StringBuilder sb = new StringBuilder(); // string buffer of ASM file
                            // *** ASM file strings
                            sb.AppendLine("; ANTIC F 320x192 + Wide Bit-> 256x192");
                            sb.AppendLine("; Display List");
                            sb.AppendLine("    org $2000");
                            sb.AppendLine("SDLSTL = $0230; Display List starting address");
                            sb.AppendLine("NP32chr = $21; Narrow playfield(32 bytes / chars)");
                            sb.AppendLine("SDMCTL = $022F; https://atariwiki.org/wiki/Wiki.jsp?page=SDMCTL");
                            sb.AppendLine("blank1 = $00; 1 blank lines");
                            sb.AppendLine("blank8 = $70; 8 blank lines");
                            sb.AppendLine("antic2 = $02; ANTIC F mode");
                            sb.AppendLine("anticF = $0F; ANTIC F mode");
                            sb.AppendLine("anticE = $0E; ANTIC F mode");
                            sb.AppendLine("lms = $40; Load memory scan");
                            sb.AppendLine("jvb = $41; Jump while vertical blank");
                            sb.AppendLine("STICK0 = $0278; Joystick 0");
                            sb.AppendLine("STRIG0 = $0284; Trigger 0");
                            sb.AppendLine("; Color registers");
                            sb.AppendLine("cr_CHR = 709; char color");
                            sb.AppendLine("cr_BG = 710; screen background");
                            sb.AppendLine("; Load Display List");
                            sb.AppendLine("    mwa #dlist SDLSTL");
                            sb.AppendLine("; Main");
                            sb.AppendLine("    lda #NP32chr");
                            sb.AppendLine("    sta SDMCTL; set 32 byte narrow screen");
                            sb.AppendLine("main");
                            sb.AppendLine("    lda STRIG0");
                            sb.AppendLine("    cmp #0");
                            sb.AppendLine("    beq bw");
                            sb.AppendLine("    lda #0");
                            sb.AppendLine("    sta cr_BG; set screen backgroud to BLACK");
                            sb.AppendLine("    lda #15");
                            sb.AppendLine("    sta cr_CHR; set char color to WHITE");
                            sb.AppendLine("    jmp main");
                            sb.AppendLine("bw");
                            sb.AppendLine("    lda #0");
                            sb.AppendLine("    sta cr_CHR; set char color to BLACK");
                            sb.AppendLine("    lda #15");
                            sb.AppendLine("    sta cr_BG; set screen backgroud to WHITE");
                            sb.AppendLine("    jmp main");
                            sb.AppendLine("; Display List");
                            sb.AppendLine("dlist");
                            sb.AppendLine("    :2 dta blank8");
                            sb.AppendLine("    .byte antic2 +lms, < github_line, > github_line");
                            sb.AppendLine("    .byte blank1");
                            sb.AppendLine("    .byte anticF +lms, < screen, > screen");
                            sb.AppendLine("    :118 dta anticF");
                            sb.AppendLine("    .byte anticF +lms, < ($3000), > ($3000)");
                            sb.AppendLine("    :72 dta anticF");
                            sb.AppendLine("    .byte blank1");
                            sb.AppendLine("    .byte antic2");
                            sb.AppendLine("    .byte jvb, < dlist, > dlist");
                            sb.AppendLine("; Data");
                            sb.AppendLine("zero_data");
                            sb.AppendLine("    :33 dta $00");
                            sb.AppendLine("screen");
                        // ZX screen memory format: 3 pages, 8 blocks of 8 lines per page
                        for (var pages = 0; pages < 3; pages++)
                        {
                            for (var eightlines = 0; eightlines < 8; eightlines++)
                            {
                                for (var eightlineblock = 0; eightlineblock < 8; eightlineblock++)
                                {
                                    sb.Append("    .byte "); // ASM block of picture
                                    // constant text hex block $FF, $FF ...
                                    byte[] hex_str = {  0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x2C, 0x20,
                                                        0x24, 0x30, 0x30, 0x2C, 0x20, 0x24, 0x30, 0x30, 0x0A };
                                    // constant hex char code 1..F
                                    var num_x = Enumerable.Range(0, 32).ToList(); // Parallel.ForEach total columns (BYTE) to convert in one line
                                    Parallel.ForEach(num_x, x => // cycle range of picture one line (32 bytes)
                                    {
                                        byte color = bytes[(pages * 2048) + (eightlineblock * 256) + eightlines * 32 + x]; // temp variables of color
                                        if (bytes.Length > 6144) // detect attributes
                                        {
                                            int attr_pos = 6144 + x + (png_y >>> 3) * 32; // get position in attributes
                                            switch (bytes[attr_pos]) // change color variables
                                            {
                                                case 0:
                                                case 64:
                                                    color = 0;
                                                    break;
                                                case 63:
                                                case 127:
                                                    color = 255;
                                                    hex_str[x * 5 + 1] = 0x46;
                                                    hex_str[x * 5 + 2] = 0x46;
                                                    break;
                                                case 56:
                                                case 120:
                                                    color = (byte)~color;
                                                    hex_str[x * 5 + 1] = hex[color >> 4];
                                                    hex_str[x * 5 + 2] = hex[color & 0x0F];
                                                    break;
                                                default:
                                                    hex_str[x * 5 + 1] = hex[color >> 4];
                                                    hex_str[x * 5 + 2] = hex[color & 0x0F];
                                                    break;
                                            }
                                        }
                                        png[png_y * 32 + x] = color; // copy byte from SCR picture to PNG byte array
                                    });
                                    // generate hexadecimal sting
                                    sb.Append(System.Text.Encoding.UTF8.GetString(hex_str, 0, hex_str.Length));
                                    png_y++; // change line position of PNG
                                }
                            }
                        }
                        // finally ASM file
                        sb.AppendLine("copyright");
                        sb.AppendLine("    .byte \"ATARI ZXART VIEWER 2023 ZARA6502\"");
                        sb.AppendLine("github_line");
                        sb.AppendLine("    .byte \" github.com/zara6502/scr2atari  \"");
                        // write ASM to disk
                        System.IO.File.WriteAllText(listfiles[i] + ".asm", sb.ToString());
                        if ((options & nopng) == 0)
                        {
                            // *** convert PNG byte array to bitmap
                            Bitmap destination = new Bitmap(256, 192, PixelFormat.Format1bppIndexed);
                            destination.SetResolution(96, 96);
                            BitmapData destinationData = destination.LockBits(new Rectangle(0, 0, destination.Width, destination.Height), ImageLockMode.WriteOnly, PixelFormat.Format1bppIndexed);
                            Marshal.Copy(png, 0, destinationData.Scan0, destinationData.Stride * 192);
                            destination.UnlockBits(destinationData);
                            // *** save bitmap on disk to PNG format
                            destination.Save(listfiles[i] + ".png");
                        }
                    }
                    else
                    {
                        // exceprion write to log file and console
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine(" Incorrect file size");
                        Console.ForegroundColor = ConsoleColor.Yellow;
                        sbe.AppendLine(listfiles[i]);
                        sbe.AppendLine("Incorrect File Size : <6144 bytes");
                        sbe.AppendLine("--------------------");
                    }
                }
                catch (Exception e)
                {
                    // exceprion write to log file and console
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine(e.Message);
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    sbe.AppendLine(listfiles[i]);
                    sbe.AppendLine(e.Message);
                    sbe.AppendLine("--------------------");
                }
            });
            if ((options & noobx) == 0)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("... ATARI object file compilation");
                var num_ii = Enumerable.Range(0, listfiles.Count).ToList(); // range variables of total files
                counter = 1;
                Parallel.ForEach(num_ii, ii => // Parallel.ForEach check .obx ATARI file compilation result
                {
                    if (!File.Exists(@listfiles[ii] + ".asm"))
                        sbe.AppendLine("\"" + listfiles[ii] + ".asm\": file not created, compilation error.\n--------------------");
                    else
                    {
                        // *** compilation ASM file to ATARI binary format
                        using (System.Diagnostics.Process pProcess = new System.Diagnostics.Process())
                        {
                            pProcess.StartInfo.FileName = @"mads.exe";
                            pProcess.StartInfo.Arguments = "\"" + listfiles[ii] + ".asm\"";
                            pProcess.StartInfo.UseShellExecute = false;
                            pProcess.StartInfo.RedirectStandardOutput = true;
                            pProcess.StartInfo.WindowStyle = System.Diagnostics.ProcessWindowStyle.Hidden;
                            pProcess.StartInfo.CreateNoWindow = true;
                            pProcess.Start();
                            pProcess.WaitForExit();
                        } // *** end compilation
                          // *** out text info of current file: percent of work, time, file name
                        ts = sw.Elapsed;
                        Console.WriteLine(" {1:00}%  {2:00}h {3:00}m {4:00}s  {0}   ", Path.GetFileName(listfiles[ii]), (int)(counter * 100 / listfiles.Count), ts.Hours, ts.Minutes, ts.Seconds);
                        counter++;
                    }
                });
            }
            // write log file if buffer not null
            if (sbe.Length > 0)
            {
                System.IO.File.WriteAllText("errors.log", sbe.ToString());
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("\nErrors encountered, see 'errors.log' file.");
            }
            ts = sw.Elapsed;
            // total time
            Console.ForegroundColor = ConsoleColor.White;
            Console.WriteLine("Total time: {0:00}h {1:00}m {2:00}s", ts.Hours, ts.Minutes, ts.Seconds);
            sw.Stop();
        }
        // create file list from args[]
        public static List<string> FillFilesList(IEnumerable<string> args) {
            List<string> list = new List<string>();
            foreach (var ar in args)
                if (ar.Contains("*") || ar.Contains("?")) {
                    string temp_path = (temp_path = Path.GetDirectoryName(ar)) == "" ? Directory.GetCurrentDirectory() : temp_path;
                    list.AddRange(Directory.EnumerateFiles(temp_path, Path.GetFileName(ar)));
                } else list.Add(ar);
            return list;
        }
    }
}
