using System.Diagnostics;

public partial class TinyLZ2
{
    public void FindMinPath()
    {
        nodes[0][0].cs.elias = 8;
        nodes[0][0].Weight = 0;
        minPath.Add(nodes[0][0]);
        minWeight = int.MaxValue;
        bool minUpdated = true;
        //bool dontUseFastMethod = true;
        int minParentId = 0;
        int maxParentId = 0;
        Console.WriteLine("Find Min Path:");
        ArcProgressBar apb = new ArcProgressBar(prglen, bytes.Length, Console.CursorLeft, Console.CursorTop);
        while (minUpdated) // && dontUseFastMethod)
        {
            minUpdated = false;
            List<Node> tempMinPath = new List<Node>();
            foreach (var parentNode in minPath)
            {
                int weight = parentNode.Weight + parentNode.cs.elias;
                if (weight < endMinPathWeight)
                {
                    bool tempbool = true;
                    foreach (var childNode in nodes[parentNode.ParentThreadId + parentNode.cs.len])
                    {
                        /*if (childNode.cs.litType == LiteralType.LastOffset &&
                            childNode.LastOffsetParentNode != parentNode) 
                            continue;*/
                        //if (parentNode.cs.litType == LiteralType.BytesSequence &&
                          //  childNode.cs.litType != LiteralType.Literal) continue;
                        if (weight < childNode.Weight)
                        {
                            if (minParentId > childNode.ParentThreadId || tempbool)
                            {
                                minParentId = childNode.ParentThreadId;
                                tempbool = false;
                            }
                            if (maxParentId < childNode.ParentThreadId)
                                maxParentId = childNode.ParentThreadId;
                            tempMinPath.Add(childNode);
                            minUpdated = true;
                            minWeight = weight;
                            childNode.Weight = weight;
                            childNode.MinPathParentNode = parentNode;
                            if (childNode.cs.litType == LiteralType.None)
                            {
                                //dontUseFastMethod = false;
                                if (endMinPathWeight == int.MaxValue)
                                {
                                    fastPathWeight = weight;
                                    goto End;
                                }
                                if (endMinPathWeight > weight) endMinPathWeight = weight;
                            }
                        }
                    }
                }
                parentNode.Delete = true;
            }
            minPath = new List<Node>(tempMinPath);
            apb.Tick(minParentId, maxParentId);
        }
    End:
        apb.Stop(minParentId, maxParentId);
        /*Console.WriteLine("Optimal encode path finded on {0} byte from {1} bytes ({2}%)",
            minParentId, bytes.Length, (int)(minParentId * 100 / bytes.Length));
        Console.WriteLine("First and fastest encode path doing pack to {0} bytes ({1}%)",
            (int)(fastPathWeight / 8 + (fastPathWeight % 8 == 0 ? 0 : 1)), (int)((fastPathWeight + (fastPathWeight % 8 == 0 ? 0 : 1)) * 100 / 8 / bytes.Length));
        Console.WriteLine("Optimal encode path doing pack to {0} bytes ({1}%)",
            (int)(endMinPathWeight / 8 + (endMinPathWeight % 8 == 0 ? 0 : 1)), (int)((endMinPathWeight + (endMinPathWeight % 8 == 0 ? 0 : 1)) * 100 / 8 / bytes.Length));
        */
    }
}