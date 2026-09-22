using System.Diagnostics;

public class ProgressBar
{
    const string rotateString = @"|/-\";
    const string filledCell = "▓";
    const string altFilledCell = "#";
    const string estimatedTimeString = "Estimated time: ";
    const string elapsedTimeString = "Elapsed time  : ";
    const string startedCell = "[";
    const string endingCell = "]";
    const char emptyCell = '.';
    private Stopwatch sw;
    private int Length;
    private int ticks = 0;
    private int totalTicks;
    private int currpos = 0;
    private int currpos_rotate = 0;
    private int lastpos = 0;
    private int lastpos_rotate = 0;
    private int rotate_index = 0;
    private int cursorleft;
    private int cursortop;

    public ProgressBar(int stringbarlength, int totalticks, int x, int y)
    {
        Length = stringbarlength - 2;
        totalTicks = totalticks;
        cursorleft = x + 1;
        cursortop = y;
        Console.Write("{0}{1}{2}", startedCell,
            new string(emptyCell, stringbarlength - startedCell.Length - endingCell.Length), endingCell);
        Console.WriteLine("\n" + estimatedTimeString);
        Console.WriteLine(elapsedTimeString);
        Start();
    }
    public void Start()
    {
        Console.CursorVisible = false;
        sw = new Stopwatch();
        sw.Start();
    }
    public void Stop()
    {
        sw.Stop();
        Console.SetCursorPosition(cursorleft, cursortop);
        Console.Write(filledCell);
        Console.WriteLine("\n\n");
        Console.CursorVisible = true;
    }
    public void Tick(bool markFastPath = false)
    {
        currpos = (int)((double)ticks / (double)totalTicks * (double)Length);
        currpos_rotate = (int)((double)ticks / (double)totalTicks * (double)Length * 100.0);
        if (currpos_rotate != lastpos_rotate)
            PrintRotateCell();
        if (currpos != lastpos)
            PrintBar(markFastPath ? altFilledCell : filledCell);
        ticks++;
    }
    private void PrintBar(string cell)
    {
        Console.SetCursorPosition(cursorleft, cursortop);
        Console.Write(cell);
        cursorleft = Console.CursorLeft;
        cursortop = Console.CursorTop;
        Console.WriteLine();
        Console.SetCursorPosition(estimatedTimeString.Length, Console.CursorTop);
        long sw_save = sw.ElapsedMilliseconds;
        //double ms = (sw_save / ticks) * (totalTicks - ticks);
        double ms = (totalTicks / ticks) * sw_save;
        int h = (int)(ms / 3600000);
        int m = (int)((ms % 3600000) / 60000);
        if (h == 0 && m == 0)
        {
            m = 1;
            Console.Write("<");
        }
        else
            Console.Write(">");
        Console.Write("{0}h {1}m     \n", h, m);
        Console.SetCursorPosition(elapsedTimeString.Length, Console.CursorTop);
        h = (int)(sw_save / 3600000);
        m = (int)((sw_save % 3600000) / 60000);
        Console.Write(">{0}h {1}m", h, m);
        lastpos = currpos;
    }
    private void PrintRotateCell()
    {
        Console.SetCursorPosition(cursorleft, cursortop);
        Console.Write(rotateString[rotate_index]);
        //-------------
        Console.WriteLine();
        Console.SetCursorPosition(estimatedTimeString.Length, Console.CursorTop);
        long sw_save = sw.ElapsedMilliseconds;
        //double ms = (sw_save / ticks) * (totalTicks - ticks);
        double ms = (totalTicks / ticks) * sw_save;
        int h = (int)(ms / 3600000);
        int m = (int)((ms % 3600000) / 60000);
        if (h == 0 && m == 0)
        {
            m = 1;
            Console.Write("<");
        }
        else
            Console.Write(">");
        Console.Write("{0}h {1}m     \n", h, m);
        Console.SetCursorPosition(elapsedTimeString.Length, Console.CursorTop);
        h = (int)(sw_save / 3600000);
        m = (int)((sw_save % 3600000) / 60000);
        Console.Write(">{0}h {1}m", h, m);
        //-------------
        rotate_index++;
        if (rotate_index == rotateString.Length) rotate_index = 0;
        lastpos_rotate = currpos_rotate;
    }
}