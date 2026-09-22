using System.Data;
using System.Text;

public partial class TinyLZ2
{
    int[] ZaraTable = new int[255];
    const int INITIAL_OFFSET = 1;
    const int FALSE = 0;
    const int TRUE = 1;

    public class Block
    {
        public Block chain;
        public Block ghost_chain;
        public int bits;
        public int index;
        public int offset;
        public int references;
        public bool isempty;
    }

    public Block CreateNode(int bits, int index, int offset, Block chain, bool isempty = false)
    {
        Block newBlock = new Block
        {
            bits = bits,
            index = index,
            offset = offset,
            chain = chain,
            isempty = isempty
        };
        return newBlock;
    }

    public void SetNewBlockInArray(ref Block ptr, Block chain)
    {
        ptr = chain;
    }

    public Block optimize(byte[] bytes, int prglen)
    {
        Block[] last_literal = new Block[bytes.Length];
        Block[] last_match = new Block[bytes.Length];
        Block[] optimal = new Block[bytes.Length];
        int[] match_length = new int[bytes.Length];
        int[] best_length = new int[bytes.Length];
        int best_length_size;
        int bits;
        int i;
        int offset;
        int length;
        int bits2;
        int dots = 1;
        int max_offset = OffsetInEdges(bytes.Length - 1, offsetMax);

        for (int i2 = 0; i2 < ZaraTable.Length; i2++) ZaraTable[i2] = bitsZara(i2, true);

        if (bytes.Length > 2)
            best_length[2] = 2;

        SetNewBlockInArray(ref last_match[INITIAL_OFFSET], CreateNode(-1, -1, INITIAL_OFFSET, new Block()));

        Console.Write("[");

        for (i = 0; i < bytes.Length; i++)
        {
            best_length_size = 2;
            max_offset = OffsetInEdges(i, offsetMax);
            for (offset = 1; offset <= max_offset; offset++)
            {
                if (i != 0 && i >= offset && bytes[i] == bytes[i - offset])
                {
                    if (last_literal[offset] != null)
                    {
                        length = i - last_literal[offset].index;
                        bits = last_literal[offset].bits + 1 + Elias(length);
                        SetNewBlockInArray(ref last_match[offset], CreateNode(bits, i, offset, last_literal[offset]));
                        if (optimal[i] == null || optimal[i].bits > bits)
                            SetNewBlockInArray(ref optimal[i], last_match[offset]);
                    }
                    if (++match_length[offset] > 1)
                    {
                        if (best_length_size < match_length[offset])
                        {
                            bits = optimal[i - best_length[best_length_size]].bits + Elias(best_length[best_length_size] - 1);
                            do
                            {
                                best_length_size++;
                                bits2 = optimal[i - best_length_size].bits + Elias(best_length_size - 1);
                                if (bits2 <= bits)
                                {
                                    best_length[best_length_size] = best_length_size;
                                    bits = bits2;
                                }
                                else
                                {
                                    best_length[best_length_size] = best_length[best_length_size - 1];
                                }
                            } while (best_length_size < match_length[offset]);
                        }
                        length = best_length[match_length[offset]];
                        //bits = optimal[i - length].bits + 8 + Elias((offset - 1) / 128 + 1) + Elias(length - 1);
                        bits = optimal[i - length].bits + 8 + bitsZara((offset - 1) / 256) + Elias(length - 1);
                        if (last_match[offset] == null || last_match[offset].index != i || last_match[offset].bits > bits)
                        {
                            SetNewBlockInArray(ref last_match[offset], CreateNode(bits, i, offset, optimal[i - length]));
                            if (optimal[i] == null || optimal[i].bits > bits)
                                SetNewBlockInArray(ref optimal[i], last_match[offset]);
                        }
                    }
                }
                else
                {
                    match_length[offset] = 0;
                    if (last_match[offset] != null)
                    {
                        length = i - last_match[offset].index;
                        bits = last_match[offset].bits + 1 + Elias(length) + length * 8;
                        SetNewBlockInArray(ref last_literal[offset], CreateNode(bits, i, 0, last_match[offset]));
                        if (optimal[i] == null || optimal[i].bits > bits)
                            SetNewBlockInArray(ref optimal[i], last_literal[offset]);
                    }
                }
            }

            if (i * prglen / bytes.Length > dots)
            {
                Console.Write(".");
                Console.Out.Flush();
                dots++;
            }
        }

        Console.WriteLine("]");

        return optimal[bytes.Length - 1];
    }

    static int OffsetInEdges(int index, int offset_limit)
    {
        return index > offset_limit ? offset_limit : index < INITIAL_OFFSET ? INITIAL_OFFSET : index;
    }

    static int Elias(int value)
    {
        int bits = 1;
        while ((value >>= 1) != 0)
            bits += 2;
        return bits;
    }
    int bitsZara(int value, bool preCalc = false)
    {
        if (!preCalc && value < ZaraTable.Length) return ZaraTable[value];
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


