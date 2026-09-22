using System.Diagnostics;

public class ArcProgressBar
{
    const string rotateString = @"|/-\";
    const string filledCell = "▓";
    const string altFilledCell = "░";
    const string estimatedTimeString = "Estimated time: ";
    const string elapsedTimeString = "Elapsed time  : ";
    const string startedCell = "[";
    const string endingCell = "]";
    const char emptyCell = '.';
    private Stopwatch sw;
    private int Length;
    private int ticks = 0;
    private int ticks_alt = 0;
    private int totalTicks;
    private int currpos = 0;
    private int currpos_alt = 0;
    private int currpos_rotate = 0;
    private int lastpos = 0;
    private int lastpos_rotate = 0;
    private int lastpos_alt = 0;
    private int rotate_index = 0;
    private int cursorleft;
    private int cursortop;
    private int cursorleft_alt;
    private int cursortop_alt;

    public ArcProgressBar(int stringbarlength, int totalticks, int x, int y)
    {
        Length = stringbarlength - 2;
        totalTicks = totalticks;
        cursorleft = x + 1;
        cursortop = y;
        cursorleft_alt = x + 1;
        cursortop_alt = y;
        Console.Write("{0}{1}{2}", startedCell,
            new string(emptyCell, stringbarlength - startedCell.Length - endingCell.Length), endingCell);
        Start();
    }
    public void Start()
    {
        Console.CursorVisible = false;
        sw = new Stopwatch();
        sw.Start();
    }
    public void Stop(int tick, int tick_alt)
    {
        sw.Stop();
        Tick(tick, tick_alt);
        Console.SetCursorPosition(cursorleft, cursortop);
        Console.Write(filledCell);
        Console.WriteLine();
        Console.CursorVisible = true;
    }
    public void Tick(int tick, int tick_alt)
    {
        do
        {
            currpos = (int)((double)ticks * (double)Length / (double)totalTicks);
            currpos_alt = (int)((double)ticks_alt * (double)Length / (double)totalTicks);
            currpos_rotate = (int)((double)ticks * (double)Length / (double)totalTicks * 100.0);
            if (lastpos_alt != currpos_alt)
            {
                PrintBarAlt(currpos_alt, altFilledCell);
                lastpos_alt = currpos_alt;
            }
            if (lastpos != currpos)
            {
                PrintBar(currpos, filledCell);
                lastpos = currpos;
            }
            PrintRotateCell();
            if (ticks < tick) ticks++;
            if (ticks_alt < tick_alt) ticks_alt++;
        } while (ticks < tick || ticks_alt < tick_alt);
    }
    private void PrintBarAlt(int pos, string cell)
    {
        Console.SetCursorPosition(cursorleft_alt, cursortop_alt);
        Console.Write(cell);
        cursorleft_alt = Console.CursorLeft;
        cursortop_alt = Console.CursorTop;
    }
    private void PrintBar(int pos, string cell)
    {
        Console.SetCursorPosition(cursorleft, cursortop);
        Console.Write(cell);
        cursorleft = Console.CursorLeft;
        cursortop = Console.CursorTop;
    }
    private void PrintRotateCell()
    {
        Console.SetCursorPosition(cursorleft, cursortop);
        Console.Write(rotateString[rotate_index]);
        rotate_index++;
        if (rotate_index == rotateString.Length) rotate_index = 0;
        lastpos_rotate = currpos_rotate;
    }
}
