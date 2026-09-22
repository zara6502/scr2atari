/*public class CodeSequenceRecord : IComparable<CodeSequenceRecord>
{
    public bool Selected;
    public bool StartNode;
    public int ParentThreadId;
    public int SelfThreadId;
    public CodeSequence cs;
    public CodeSequenceRecord(bool _selected, bool _startnode, int _parentthreadid, int _selfthreadid, CodeSequence _cs)
    {
        Selected = _selected;
        StartNode = _startnode;
        ParentThreadId = _parentthreadid;
        SelfThreadId = _selfthreadid;
        cs = _cs;
    }
    public int CompareTo(CodeSequenceRecord other)
    {
        if (other == null || other.cs.len > this.cs.len)
        {
            return 1;
        }
        if (ReferenceEquals(this, other) || other.cs.len == this.cs.len)
        {
            return 0;
        }
        return -1;
    }
}*/