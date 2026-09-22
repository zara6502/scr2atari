using System.Diagnostics;
using System.Reflection;
using System.Text;
using static System.Net.Mime.MediaTypeNames;

public partial class TinyLZ2
{
    public List<int>[] list_letterPos = new List<int>[256];
    public List<Node> minPath = new List<Node>();
    public List<Node>[] nodes;
    public Node minPathStartNode = new();
    public int endMinPathWeight = int.MaxValue;
    public int fastPathWeight = int.MaxValue;
    public int how = 0;
    public int minWeight;
    public int NodesCount;
    public int prglen;
    public int TotalBits = 0;
    public int TotalBytes = 0;
    public int[] EliasTable = new int[2048];
    public int[] EliasGammaTable = new int[2048];
    public byte[] bytes;
    const int maxLength = 2;
    const int dividerOffset = 127;
    const int prefixBitLen = 0;
    const int literalBitLen = 8;
    const int offsetMax = dividerOffset << 8;
    const int lastOffset_literalInterval = 10;
    const bool avgOn = true;
    const int minRLELength = 25;
    const int minLastOffsetInterval = 2;
    int RLECount = 0;
    int RLESumLen = 0;
    int LastOffsetCount = 0;
    int LastOffsetSumLen = 0;

    public TinyLZ2(byte[] b, int pl)
    {
        bytes = b;
        prglen = pl;
        for (int i = 0; i < list_letterPos.Length; i++)
            list_letterPos[i] = new();
        for (int i = 0; i < EliasTable.Length; i++)
            EliasTable[i] = bitsElias(i, true);
        for (int i = 1; i < EliasGammaTable.Length; i++)
            EliasGammaTable[i] = bitsEliasGamma(i, true);
        nodes = new List<Node>[bytes.Length + 1];
        for (int i = 0; i < nodes.Length; i++)
            nodes[i] = new();
        FillBytesPositionsArray(bytes);

        var optimal = optimize(b, prglen);
        Console.ForegroundColor = ConsoleColor.DarkGreen;
        Console.WriteLine("ZX0 bytes = {0} / {1} ({2}%)", (int)(optimal.bits / 8), bytes.Length, (int)(optimal.bits * 100 / 8 / bytes.Length));
        Console.ResetColor();
        Block prev = null;
        while (optimal != null)
        {
            Block next = optimal.chain;
            optimal.chain = prev;
            prev = optimal;
            optimal = next;
        }
        PrintZX0(prev);
        GetCodeSequence();
        //int j = 0;
        /*for(; j < nodes.Length; j++)
        {
            Console.Write("\n{0} : ", j);
            foreach (var cl in nodes[j])
            {
                int new_ParentId = j + cl.cs.len;
                int ji = j;
                foreach (var newcl in nodes[ji])
                {
                    Console.Write("{0}, ", newcl.ParentThreadId);
                }
            }
        }*/
    }
    public void PrintZX0(Block prev)
    {
        int last_offset = 1;
        int input_index = 0;
        int lo_count = 0;
        int lo_sum = 0;
        for (Block optimal = prev.chain; optimal != null; prev = optimal, optimal = optimal.chain)
        {
            int length = optimal.index - prev.index;
            if (length < 0) continue;
            if (optimal.offset == 0)
            {
                Console.ForegroundColor = ConsoleColor.DarkYellow;
                Console.Write("{0, 4} 0 |", input_index);
                for (int i = 0; i < length; i++)
                {
                    byte[] jByte = new byte[1];
                    Array.Copy(bytes, input_index, jByte, 0, 1);
                    Console.Write("{0}", CleanupString(Encoding.Default.GetString(jByte)));
                    input_index++;
                }
                Console.WriteLine("|");
            }
            else if (optimal.offset == last_offset)
            {
                lo_count++;
                lo_sum += length;
                byte[] jByte = new byte[length];
                Array.Copy(bytes, input_index, jByte, 0, length);
                Console.ForegroundColor = ConsoleColor.DarkCyan;
                Console.WriteLine("{0, 4} 0 |{1}|\t\t{2}", input_index, CleanupString(Encoding.Default.GetString(jByte)), optimal.offset);
                input_index += length;
            }
            else
            {
                byte[] jByte = new byte[length];
                Array.Copy(bytes, input_index, jByte, 0, length);
                Console.ForegroundColor = ConsoleColor.DarkGreen;
                Console.WriteLine("{0, 4} 1 |{1}|\t\t{2}", input_index, CleanupString(Encoding.Default.GetString(jByte)), optimal.offset);
                input_index += length;
                last_offset = optimal.offset;
            }
        }
        Console.ResetColor();
        Console.WriteLine("LO count = {0}, sum = {1}", lo_count, lo_sum);
    }
    public void FillBytesPositionsArray(byte[] b)
    {
        for (int i = 0; i < b.Length; i++)
            list_letterPos[b[i]].Add(i);
        for (int i = 0; i <= 255; i++)
            if (list_letterPos[i].Count > 0)
                list_letterPos[i].Reverse();
    }
    private void GetCodeSequence()
    {
        int i = 0;
        Console.WriteLine("Create linked list:");
        ProgressBar pb = new ProgressBar(prglen, bytes.Length, Console.CursorLeft, Console.CursorTop);
        for (; i < bytes.Length; )
        {
            int old_i = i;
            i = CreateNodesList2(i, list_letterPos[bytes[i]]);
            for (; old_i < i; old_i++)
                pb.Tick();
        }
        nodes[bytes.Length].Add(new Node(LiteralType.None, bytes.Length, null,
                                new CodeSequence(0, 0, 0, 0, LiteralType.None, 0, 0), int.MaxValue));
        pb.Stop();
        for (int p = 0; p < nodes.Length; p++)
        {
            NodesCount += nodes[p].Count;
        }
        Console.WriteLine("NodesCount before delete = {0} in file {1} bytes", NodesCount, bytes.Length);
        NodesCount = 0;
        for (int p = 0; p < nodes.Length; p++)
        {
            nodes[p].RemoveAll(x => x.Delete);
            NodesCount += nodes[p].Count;
        }
        Console.WriteLine("NodesCount after  delete = {0} in file {1} bytes", NodesCount, bytes.Length);
        Console.WriteLine("RLE Count = {0}, RLE Sum Len = {1}", RLECount, RLESumLen);
        Console.WriteLine("LastOffset Count = {0}, LastOffset Sum Len = {1}", LastOffsetCount, LastOffsetSumLen);
        //SaveNodeToFile();
        //FindAndCreateLastOffsetNodes();
        FindMinPath();
        minPathStartNode = nodes[0][0];
        minPath = CreateFinalMinPathList(nodes[nodes.Length - 1][0]);

        int changeCount = 1;
        var m = minPath[0].cs.litType;
        int literalSeries = 1;
        var lastCS = minPath[0].cs.litType;
        for (int p = 1; p < minPath.Count; p++)
        {
            if (minPath[p].cs.litType != m)
            {
                m = minPath[p].cs.litType;
                changeCount++;
            }

            if (minPath[p].cs.litType == lastCS && lastCS == LiteralType.Literal)
            {
                literalSeries++;
                continue;
            }

            if (minPath[p].cs.litType != lastCS && lastCS == LiteralType.BytesSequence)
            {
                literalSeries++;
                lastCS = LiteralType.Literal;
                continue;
            }
            if (minPath[p].cs.litType != lastCS && lastCS == LiteralType.Literal)
            {
                TotalBits += bitsElias(literalSeries);
                literalSeries = 0;
                lastCS = LiteralType.BytesSequence;
                continue;
            }

            if (minPath[p].cs.litType == lastCS && lastCS == LiteralType.BytesSequence)
            {
                TotalBits++;
                continue;
            }
        }
        Console.WriteLine("ChangeCount = {0}, minPath count = {1}", changeCount, minPath.Count);
        TotalBytes = TotalBits / 8 + (((TotalBits % 8) == 0) ? 0 : 1);
        Console.ForegroundColor = ConsoleColor.DarkCyan;
        Console.WriteLine("Total bytes 2 = {0} / {1}", TotalBytes, bytes.Length);
        Console.ResetColor();
        //GenerateReportCodeSequence();
    }
    public void FindAndCreateLastOffsetNodes()
    {
        List<List<Node>> tempListNodes = new List<List<Node>>();
        for (int ParentId = 1; ParentId < bytes.Length; ParentId++)
        {
            for (int j = 0; j < nodes[ParentId].Count; j++)
            {
                if (nodes[ParentId][j].cs.litType != LiteralType.BytesSequence) continue;
                List<Node> tempNodes = new List<Node>();
                //bool LastOffsetBlockIsDone = false;
                tempNodes.Add(nodes[ParentId][j]);
                for (int k = ParentId + 1; k < (bytes.Length - 3) && k < (ParentId + 1 + lastOffset_literalInterval); k++)
                {
                    int m = 0;
                    for (; m < nodes[k].Count; m++)
                        if (nodes[k][m].cs.litType == LiteralType.Literal) break;
                    tempNodes.Add(nodes[k][m]);
                    int n = 0;
                    for (; n < nodes[k + 1].Count; n++)
                        if (nodes[k + 1][n].cs.litType == LiteralType.BytesSequence &&
                            (nodes[k + 1][n].cs.currentpos - nodes[k + 1][n].cs.letterpos) ==
                            (nodes[ParentId][j].cs.currentpos - nodes[ParentId][j].cs.letterpos)) break;
                }
            }
        }
    }
    public void CreateNodesList(int i, List<int> list_letterPos)
    {
        bool above9bit = false;
        int ParentId = i;
        foreach (var letter_pos in list_letterPos)
        {
            if (letter_pos >= i) continue;
            int tempOffset = i - letter_pos;
            if (tempOffset > offsetMax) continue;
            int tempLen = CompareBytes(i, letter_pos);
            int tempOffsetMSB = (int)((tempOffset - 1) / dividerOffset);
            int tempOffsetLSB = (tempOffset - 1) % dividerOffset << 1;
            int eliasTempOffsetMSB = bitsElias(tempOffsetMSB);
            int eliasTempOffsetLSB = bitsElias(tempOffsetLSB);
            int temp_elias = prefixBitLen + bitsElias(tempLen - 1) + eliasTempOffsetMSB + eliasTempOffsetLSB;
            if (tempLen == 1 && temp_elias >= literalBitLen) continue;
            if (Check_lenContains(ParentId, tempLen, temp_elias)) continue;
            //if (tempLen == 1 && temp_elias < literalBitLen) above9bit = true;
            nodes[ParentId].Add(new Node(LiteralType.BytesSequence, ParentId, null, new CodeSequence(
                    letter_pos, i, tempLen, temp_elias, LiteralType.BytesSequence, tempOffsetMSB, tempOffsetLSB), int.MaxValue));
        }
        if (!above9bit) nodes[ParentId].Add(new Node(LiteralType.Literal, ParentId, null, new CodeSequence(i, i, literalBitLen), int.MaxValue));
    }
    public int CreateNodesList2(int i, List<int> list_letterPos)
    {
        //int tempOffsetElias = bitsElias(tempOffset - 1);
        //int eliasTempOffsetMSB = bitsZara(tempOffsetMSB);
        //int eliasTempOffsetLSB = bitsElias(tempOffsetLSB);
        //int temp_elias = prefixBitLen + bitsZara(tempLen - 1) + eliasTempOffsetMSB + eliasTempOffsetLSB;
        //int temp_elias = prefixBitLen + bitsElias(tempLen - 1) + tempOffsetElias;
        Console.ForegroundColor = ConsoleColor.Red;
        Console.WriteLine("{0}   ", i);
        Console.ResetColor();
        //Console.ReadLine();
        bool above9bit = false;
        int ParentId = i;
        double min_avg = 9.0;
        double curr_avg = 9.0;
        bool minAvg_changed = false;
        foreach (var letter_pos in list_letterPos)
        {
            if (letter_pos >= i) continue;
            int tempOffset = i - letter_pos;
            if (tempOffset > offsetMax) continue;

            int temp_len_lastoffset = 0;
            int eliasLastOffset = 0;
            /*if (FindLastOffset(letter_pos, i, ref temp_len_lastoffset, ref eliasLastOffset))
            {
                LastOffsetCount++;
                LastOffsetSumLen += temp_len_lastoffset - i;

                for (int p = 0; p < nodes.Length; p++)
                {
                    if (nodes[p].Count > 1)
                        for (int x = 0; x < nodes[p].Count; x++)
                            if (nodes[p][x].cs.litType == LiteralType.BytesSequence && (nodes[p][x].cs.currentpos + nodes[p][x].cs.len) > i) nodes[p][x].Delete = true;
                }
                i = temp_len_lastoffset - 1;
                goto LastOffset;
            }*/

            int temp_len_rle = 0;
            if ((letter_pos + 1) == i && bytes[letter_pos] == bytes[i] && Check_RLE(i, ref temp_len_rle))
            {
                RLECount++;
                RLESumLen += temp_len_rle;
                nodes[ParentId].Add(new Node(LiteralType.BytesSequence, ParentId, null, new CodeSequence(
                        letter_pos, i, temp_len_rle,
                        prefixBitLen + bitsElias(temp_len_rle - maxLength) +
                        bitsElias((int)((tempOffset - 1) / dividerOffset)) +
                        bitsZaraCalc((tempOffset - 1) % dividerOffset << 1),
                        LiteralType.BytesSequence, (int)((tempOffset - 1) / dividerOffset), (tempOffset - 1) % dividerOffset << 1), int.MaxValue));
                i += temp_len_rle - 1;
                goto RLE;
            }

            int tempLenList = CompareBytes(i, letter_pos);
            if (tempLenList < maxLength) continue;
            int tempLen = tempLenList;
            int tempOffsetMSB = (int)((tempOffset - 1) / dividerOffset);
            int tempOffsetLSB = (tempOffset - 1) % dividerOffset << 1;
            int eliasTempOffsetMSB = bitsElias(tempOffsetMSB);
            int eliasTempOffsetLSB = bitsZaraCalc(tempOffsetLSB);
            int temp_elias = prefixBitLen + bitsElias(tempLen - maxLength) + eliasTempOffsetMSB + eliasTempOffsetLSB;
            minAvg_changed = false;
            if (avgOn)
            {
                curr_avg = temp_elias / tempLen;
                if (curr_avg >= min_avg) continue;
                minAvg_changed = true;
            }
            if (tempLen == 1 && temp_elias >= literalBitLen) continue;
            if (Check_lenContains(ParentId, tempLen, temp_elias)) continue;
            if (tempLen == 1 && temp_elias < literalBitLen) above9bit = true;
            if (minAvg_changed) min_avg = curr_avg;
            nodes[ParentId].Add(new Node(LiteralType.BytesSequence, ParentId, null, new CodeSequence(
                    letter_pos, i, tempLen, temp_elias, LiteralType.BytesSequence, tempOffsetMSB, tempOffsetLSB), int.MaxValue));
        }
        if (!above9bit) nodes[ParentId].Add(new Node(LiteralType.Literal, ParentId, null, new CodeSequence(i, i, literalBitLen), int.MaxValue));
    RLE:
    LastOffset:
        return i + 1;
    }
    public bool FindLastOffset(int letter_pos, int i, ref int temp_lenLastOffset, ref int eliasLastOffset)
    {
        List<Node> temp_nodes = new List<Node>();
        int ParentId = i;
        int tempOffset = i - letter_pos;
        int local_len = 0;
        for (; i < bytes.Length && bytes[letter_pos] == bytes[i]; i++, letter_pos++, temp_lenLastOffset++, local_len++) ;
        if (i == bytes.Length || local_len < maxLength) return false;
        int tempOffsetMSB = (int)((tempOffset - 1) / dividerOffset);
        int tempOffsetLSB = (tempOffset - 1) % dividerOffset << 1;
        int eliasTempOffsetMSB = bitsElias(tempOffsetMSB);
        int eliasTempOffsetLSB = bitsZaraCalc(tempOffsetLSB);
        int temp_elias = bitsElias(temp_lenLastOffset) + eliasTempOffsetMSB + eliasTempOffsetLSB;
        eliasLastOffset = temp_elias;
        temp_nodes.Add(new Node(LiteralType.BytesSequence, ParentId, null, new CodeSequence(
                letter_pos - temp_lenLastOffset, ParentId, temp_lenLastOffset, temp_elias, LiteralType.BytesSequence, tempOffsetMSB, tempOffsetLSB), int.MaxValue));
        Node lastOffsetParentNode = temp_nodes[0];
        int while_counter = 0;
        int lastLiteralPos = 0;
        bool copyLastNode = true;
        while (true)
        {
            copyLastNode = true;
            ParentId = i;
            lastLiteralPos = i;
            for (local_len = 0; i < bytes.Length && bytes[letter_pos] != bytes[i] && local_len <= minLastOffsetInterval; i++, letter_pos++, temp_lenLastOffset++, local_len++) ;
            if (while_counter == 0)
            {
                if (i == bytes.Length || local_len == 0 || local_len > (minLastOffsetInterval + 1)) return false;
            }
            if ((i == bytes.Length && local_len == 0) || local_len <= minLastOffsetInterval)
            {
                temp_lenLastOffset = lastLiteralPos;
                goto End;
            }
            int temp_elias_literal = bitsElias(local_len) + 8 * local_len;
            temp_nodes.Add(new Node(LiteralType.Literal, ParentId, null, new CodeSequence(ParentId, ParentId, local_len, temp_elias_literal, LiteralType.Literal), int.MaxValue));
            //for (int n = 0; n < local_len; n++)
              //  temp_nodes.Add(new Node(LiteralType.Literal, ParentId + n, null, new CodeSequence(ParentId + n, ParentId + n, 1,
                //    (n == 0 ? temp_elias_literal : 0), LiteralType.Literal), int.MaxValue));
            if (i == bytes.Length && local_len > 0)
            {
                eliasLastOffset += temp_elias_literal;
                goto End;
            }

            ParentId = i;
            for (local_len = 0; i < bytes.Length && bytes[letter_pos] == bytes[i]; i++, letter_pos++, temp_lenLastOffset++, local_len++) ;
            if (while_counter == 0)
            {
                if (local_len == 0) return false;
            }
            else
            {
                if (local_len == 0)
                {
                    temp_lenLastOffset = lastLiteralPos;
                    copyLastNode = false;
                    goto End;
                }
            }
            temp_elias = bitsElias(local_len);
            eliasLastOffset += temp_elias + temp_elias_literal;
            temp_nodes.Add(new Node(LiteralType.LastOffset, ParentId, lastOffsetParentNode, new CodeSequence(letter_pos - local_len, ParentId, local_len, temp_elias, LiteralType.LastOffset), int.MaxValue));
            if (i == bytes.Length && local_len > 0) goto End;

            while_counter++;
        }
    End:
        for (int p = 0; p < temp_nodes.Count - (copyLastNode ? 0 : 1); p++)
            nodes[temp_nodes[p].ParentThreadId].Add(new Node(temp_nodes[p].cs.litType, temp_nodes[p].ParentThreadId, null, new CodeSequence(
                temp_nodes[p].cs.letterpos, temp_nodes[p].cs.currentpos, temp_nodes[p].cs.len, temp_nodes[p].cs.elias, temp_nodes[p].cs.litType, temp_nodes[p].cs.msb, temp_nodes[p].cs.lsb), int.MaxValue));
        return true;
    }
    public bool Check_RLE(int i, ref int temp_len_rle)
    {
        byte ch = bytes[i];
        int temp_i = i;
        for (; temp_i < bytes.Length; temp_i++)
        {
            if (bytes[temp_i] != ch && temp_len_rle <= minRLELength) return false;
            if (bytes[temp_i] != ch && temp_len_rle > minRLELength)
            {
                return true;
            }
            if (bytes[temp_i] == ch) temp_len_rle++;
        }
        if (temp_len_rle <= minRLELength) return false;
        if (temp_len_rle > minRLELength) return true;
        return false;
    }
    public List<Node> CreateFinalMinPathList(Node end)
    {
        List<Node> minpathNodes = new List<Node>();
        Node tempNode = new Node();
        tempNode = end;
        while (tempNode != this.minPathStartNode)
        {
            minpathNodes.Add(tempNode);
            tempNode = tempNode.MinPathParentNode;
        }
        minpathNodes.Add(tempNode);
        TotalBits = minpathNodes[0].Weight;
        minpathNodes.Reverse();
        TotalBytes = TotalBits / 8 + (((TotalBits % 8) == 0) ? 0 : 1);
        Console.ForegroundColor = ConsoleColor.DarkCyan;
        Console.WriteLine("Total bytes = {0} / {1}", TotalBytes, bytes.Length);
        Console.ResetColor();
        return minpathNodes;
    }
    public void GenerateReportCodeSequence2(ReportType rt = ReportType.CON)
    {
        StringBuilder sb = new StringBuilder();
        foreach (var mp in minPath)
        {
            var cs = mp.cs;
            if (cs.elias == 0) continue;
            byte[] jByte = new byte[cs.len];
            Array.Copy(bytes, cs.currentpos, jByte, 0, cs.len);
            string jString = CleanupString(Encoding.Default.GetString(jByte).Replace("\r\n", "\\r\\n").Replace("\r", "\\r").Replace("\n", "\\n"));
            switch (cs.litType)
            {
                case LiteralType.Literal:
                    Console.ForegroundColor = ConsoleColor.DarkYellow;
                    Console.WriteLine("0 [{0}]{1}{2}{3}\t{4}\t{5:N2}\t{6}",
                        jString, // 0
                        PrintTabs(6, jString.Length), // 1
                        cs.len, // 2
                        "", // 3
                        cs.elias, // 4
                        0, //5
                        (cs.len == 1) ? // 6
                            cs.currentpos :
                            cs.currentpos.ToString() + "-" + (cs.currentpos + cs.len - 1).ToString());
                    break;
                case LiteralType.BytesSequence:
                    Console.ForegroundColor = ConsoleColor.Gray;
                    Console.WriteLine("1 |{0}|{1}{2}{3}\t{4}\t{5:N2}\t{6}",
                        jString, // 0
                        PrintTabs(6, jString.Length), // 1
                        cs.len, // 2
                        ", " + (cs.currentpos - cs.letterpos).ToString(), // 3
                        cs.elias, // 4
                        0, //5
                        (cs.len == 1) ? // 6
                            cs.currentpos :
                            cs.currentpos.ToString() + "-" + (cs.currentpos + cs.len - 1).ToString());
                    break;
                case LiteralType.LastOffset:
                    Console.ForegroundColor = ConsoleColor.DarkCyan;
                    Console.WriteLine("0 ({0}){1}{2}{3}\t{4}\t{5:N2}\t{6}",
                        jString, // 0
                        PrintTabs(6, jString.Length), // 1
                        cs.len, // 2
                        ", XXX", // + (mp.LastOffsetParentNode.cs.currentpos - mp.LastOffsetParentNode.cs.letterpos).ToString(), // 3
                        cs.elias, // 4
                        0, //5
                        (cs.len == 1) ? // 6
                            cs.currentpos :
                            cs.currentpos.ToString() + "-" + (cs.currentpos + cs.len - 1).ToString());
                    break;
                default:
                    Console.ForegroundColor = ConsoleColor.Gray;
                    Console.WriteLine((cs.litType == LiteralType.Literal ? "0 [" : "1 |") + "{0}" + ((cs.litType == LiteralType.Literal) ? "]" : "|") +
                        "{1}{2}{3}\t{4}\t{5:N2}\t{6}",
                        jString, // 0
                        PrintTabs(6, jString.Length), // 1
                        cs.len, // 2
                        cs.litType == LiteralType.Literal ? "" : ", " + (cs.currentpos - cs.currentpos).ToString(), // 3
                        cs.elias, // 4
                        0, //5
                        (cs.len == 1) ? // 6
                            cs.currentpos :
                            cs.currentpos.ToString() + "-" + (cs.currentpos + cs.len - 1).ToString());
                    break;
            }
        }
        Console.ForegroundColor = ConsoleColor.Gray;
        Console.WriteLine("----------------------");
        Console.ForegroundColor = ConsoleColor.DarkCyan;
        Console.WriteLine("Total Bits = {0} / {1} ({2:N2}%)", TotalBits, bytes.Length * 8, 100 * TotalBits / (bytes.Length * 8));
        Console.WriteLine("Total Bytes = {0} / {1} ({2:N2}%)", TotalBytes, bytes.Length, 100 * TotalBytes / bytes.Length);
        Console.ForegroundColor = ConsoleColor.Gray;
        Console.WriteLine("Divider = {0}", dividerOffset);
    }
    public void GenerateReportCodeSequence(ReportType rt = ReportType.CON)
    {
        bool lastIsLiteral = false;
        string literalSumString = "";
        int literalBits = 0;
        int literalBitsSum = 0;
        StringBuilder sb = new StringBuilder();
        foreach (var mp in minPath)
        {
            var cs = mp.cs;
            if (cs.elias == 0) continue;
            byte[] jByte = new byte[cs.len];
            Array.Copy(bytes, cs.letterpos, jByte, 0, cs.len);
            string jString = CleanupString(Encoding.Default.GetString(jByte).Replace("\r\n", "\\r\\n").Replace("\r", "\\r").Replace("\n", "\\n"));
            switch (cs.litType)
            {
                case LiteralType.Literal:
                    if (!lastIsLiteral)
                    {
                        lastIsLiteral = true;
                        literalSumString = jString;
                    }
                    else
                        literalSumString += jString;
                    break;
                case LiteralType.BytesSequence:
                    if (lastIsLiteral)
                    {
                        literalBits = (1 + bitsElias(literalSumString.Length) + literalSumString.Length * 8) - (literalSumString.Length * 9);
                        literalBitsSum += literalBits;
                        lastIsLiteral = false;
                        Console.ForegroundColor = ConsoleColor.DarkYellow;
                        Console.WriteLine("0\t\t[{0}]" +
                            "{1}{2}{3}\t\t{4}\t{5:N2}\t{6}",
                            literalSumString, // 0
                            PrintTabs(6, literalSumString.Length), // 1
                            literalSumString.Length, // 2
                            "", // 3
                            1 + bitsElias(literalSumString.Length) + literalSumString.Length * 8, // 4 elias
                            (double)(1 + (double)bitsElias(literalSumString.Length) + (double)literalSumString.Length * 8) / (double)literalSumString.Length, //5
                            (literalSumString.Length == 1) ? // 6
                                cs.currentpos - 1 :
                                (cs.currentpos - literalSumString.Length).ToString() + "-" + (cs.currentpos - 1).ToString());
                    }
                    Console.ForegroundColor = ConsoleColor.Gray;
                    Console.WriteLine("1\t{7}\t|{0}|" +
                        "{1}{2}{3}\t\t{4}\t{5:N2}\t{6}",
                        jString, // 0
                        PrintTabs(6, jString.Length), // 1
                        cs.len, // 2
                        ", " + (cs.currentpos - cs.letterpos).ToString(), // 3
                        cs.elias, // 4
                        0, //5
                        (cs.len == 1) ? // 6
                            cs.currentpos :
                            cs.currentpos.ToString() + "-" + (cs.currentpos + cs.len - 1).ToString(),
                        cs.msb);
                    break;
                case LiteralType.LastOffset:
                    Console.ForegroundColor = ConsoleColor.DarkCyan;
                    Console.WriteLine("0 ({0})" +
                        "{1}{2}{3}\t{4}\t{5:N2}\t{6}",
                        jString, // 0
                        PrintTabs(6, jString.Length), // 1
                        cs.len, // 2
                        ", " + (cs.currentpos - cs.letterpos).ToString(), // 3
                        cs.elias, // 4
                        0, //5
                        (cs.len == 1) ? // 6
                            cs.currentpos :
                            cs.currentpos.ToString() + "-" + (cs.currentpos + cs.len - 1).ToString());
                    break;
                default:
                    Console.ForegroundColor = ConsoleColor.Gray;
                    Console.WriteLine((cs.litType == LiteralType.Literal ? "0 [" : "1 |") + "{0}" + ((cs.litType == LiteralType.Literal) ? "]" : "|") +
                        "{1}{2}{3}\t{4}\t{5:N2}\t{6}",
                        jString, // 0
                        PrintTabs(6, jString.Length), // 1
                        cs.len, // 2
                        cs.litType == LiteralType.Literal ? "" : ", " + (cs.currentpos - cs.letterpos).ToString(), // 3
                        cs.elias, // 4
                        0, //5
                        (cs.len == 1) ? // 6
                            cs.currentpos :
                            cs.currentpos.ToString() + "-" + (cs.currentpos + cs.len - 1).ToString());
                    break;
            }
        }
        Console.ForegroundColor = ConsoleColor.Gray;
        Console.WriteLine("----------------------");
        Console.ForegroundColor = ConsoleColor.DarkCyan;
        Console.WriteLine("Total Bits = {0} / {1} ({2:N2}%)", TotalBits, bytes.Length * 8, 100 * TotalBits / (bytes.Length * 8));
        Console.WriteLine("Total Bytes = {0} / {1} ({2:N2}%)", TotalBytes, bytes.Length, 100 * TotalBytes / bytes.Length);
        Console.ForegroundColor = ConsoleColor.Gray;
        Console.WriteLine("----------------------");
        Console.ForegroundColor = ConsoleColor.DarkCyan;
        Console.WriteLine("Total Bits* = {0} / {1} ({2:N2}%)", TotalBits + literalBitsSum, bytes.Length * 8, 100 * (TotalBits + literalBitsSum) / (bytes.Length * 8));
        Console.WriteLine("Total Bytes* = {0} / {1} ({2:N2}%)", Math.Round((double)(TotalBits + literalBitsSum) / 8), bytes.Length, 100 * Math.Round((double)(TotalBits + literalBitsSum) / 8) / bytes.Length);
        Console.ForegroundColor = ConsoleColor.Gray;
        Console.WriteLine("Divider = {0}", dividerOffset);
    }
    string PrintTabs(int count, int len)
    {
        StringBuilder sb = new StringBuilder();
        for (int i = count; i > 0; i--)
        {
            sb.Append("\t");
        }
        return sb.ToString();
    }
    string PrintTabs2(int count, int len)
    {
        StringBuilder sb = new StringBuilder();
        for (int i = count - (int)((len + 4) / 8); i > 0; i--)
        {
            sb.Append("\t");
        }
        return sb.ToString();
    }
    public string CleanupString2(string str)
    {
        return new string(str.Select(ch => ch > 31 ? ch : ' ').ToArray());
    }
    public string CleanupString(string str)
    {
        StringBuilder sb = new StringBuilder();
        foreach (char ch in str)
        {
            if (ch < 32) sb.Append(@"\" + ((int)ch).ToString("X2"));
            else sb.Append(ch);
        }
        return new string(sb.ToString());
    }
    public void SaveNodeToFile()
    {
        StringBuilder sb = new StringBuilder();
        for (int p = 0; p < nodes.Length; p++)
        {
            nodes[p].RemoveAll(x => x.Delete);
            foreach (var n in nodes[p])
                sb.AppendFormat("{0} -> {1} {2}\n", p, n.cs.len + p, n.cs.elias);
        }
        byte[] bb = Encoding.ASCII.GetBytes(sb.ToString());
        File.WriteAllBytes("list.txt", bb);
    }
    public void OneStepMinPath2(int i)
    {
        /*if (NodesCount == 1)
        {
            minPath.Add(nodes[0][0]);
            minWeight = int.MaxValue;
        }
        else*/
        {
            //if (minPath.Count == 1)
              //  how++;
            foreach (var parentNode in minPath)
            {
                //if (parentNode.Delete) continue;
                //if ((parentNode.cs.len + parentNode.ParentThreadId) == i)
                {
                    foreach (var childNode in nodes[i])
                    {
                        int w = parentNode.Weight + parentNode.cs.elias;
                        if (w < childNode.Weight)
                        {
                            minWeight = w;
                            childNode.Weight = w;
                            childNode.MinPathParentNode = parentNode;
                            if (childNode.cs.litType == LiteralType.None) endMinPathWeight = w;
                        }
                    }
                    parentNode.Delete = true;
                }
                /*if (!fastFinded &&
                    (parentNode.cs.len + parentNode.ParentThreadId) == (bytes.Length - 1))
                {
                    fastPathStartNode = parentNode;
                    fastFinded = true;
                }*/
            }
            /*var setToRemove = new HashSet<Node>(minPath.Where(x => x.Delete));
            if (setToRemove.Count > 0) minPath.RemoveAll(x => setToRemove.Contains(x));*/
            if (minPath.Count > 0) minPath.RemoveAll(x => x.Delete);
            foreach (var childNode in nodes[i])
            {
                minPath.Add(childNode);
            }
        }
    }
    public int CompareBytes(int i_pos, int j_pos)
    {
        for (int i = i_pos, j = j_pos; i < bytes.Length; i++, j++)
            if (bytes[i] != bytes[j])
                return i - i_pos;
        return bytes.Length - i_pos;
    }
    public bool Check_lenContains(int ParentId, int len, int elias)
    {
        if (nodes[ParentId].Count == 0) return false;
        for (int cst_i = 0; cst_i < nodes[ParentId].Count; cst_i++)
            if (nodes[ParentId][cst_i].cs.len == len)
                if (nodes[ParentId][cst_i].cs.elias <= elias) return true;
                else nodes[ParentId][cst_i].Delete = true;
        return false;
    }
    int bitsEliasGamma(int n, bool preCalc = false)
    {
        if (!preCalc && n < EliasGammaTable.Length) return EliasGammaTable[n];
        int count = 0;
        for (int i = 2; i <= n; i <<= 1) count += 2;
        return ++count;
    }
    int bitsElias(int n, bool preCalc = false)
    {
        if (!preCalc && n < EliasTable.Length) return EliasTable[n];
        return bitsZetaXi(n, 1, 0);
    }
    int bitsZetaXi(int value, int factor, int order)
    {
        int msb = value >> order;
        int bits = order + 1;
        int n = 1;
        while (msb >= n)
        {
            msb -= n;
            n <<= factor;
            bits += factor + 1;
        }
        return bits;
    }
    int bitsZara(int value)
    {
        switch (value)
        {
            case 0: return 3;
            case 1: return 4;
            case 2: return 4;
            case > 126: return 10;
            case > 62: return 9;
            case > 30: return 8;
            case > 14: return 7;
            case > 6: return 6;
            case > 2: return 5;
            default: return 11;
        }
    }
    int bitsZaraCalc(int value)
    {
        switch (value)
        {
            case 0: return 3;
            case 1: return 4;
            case 2: return 4;
        }
        int m = 2;
        int result = 4;
        while (m < value)
        {
            m = m * 2 + 2;
            result++;
        }
        return result;
    }
}