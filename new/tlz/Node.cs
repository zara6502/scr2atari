public class Node
{
    public LiteralType litType { get; set; }
    public int ParentThreadId { get; set; }
    //public int SelfThreadId { get; set; }
    public int Weight { get; set; }
    public bool Delete { get; set; }
    //public bool StartNode { get; set; }
    //public bool Selected { get; set; }
    //public bool Root { get; set; }
    //public double dWeight { get; set; }
    public Node LastOffsetParentNode { get; set; }
    public Node MinPathParentNode { get; set; }
    //public Node PrevNode { get; set; }
    //public Node NextNode { get; set; }
    public CodeSequence cs { get; private set; }
    //public Node(int _parentthreadid, int _selfthreadid, Node lastOffsetParentNode, CodeSequence codeSequence, int weight, bool delete = false)//, CodeSequenceRecord id)
    public Node(LiteralType _litType, int _parentthreadid, Node lastOffsetParentNode, CodeSequence codeSequence, int weight, bool delete = false)//, CodeSequenceRecord id)
    {
        ParentThreadId = _parentthreadid;
        litType = _litType;
        //SelfThreadId = _selfthreadid;
        LastOffsetParentNode = lastOffsetParentNode;
        Weight = weight;
        Delete = delete;
        this.cs = codeSequence;
    }
    public Node()
    {
    }
}
