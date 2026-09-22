public class CodeSequence
{
    public int letterpos { get; set; }
    public int currentpos { get; set; }
    public int len { get; set; }
    public int elias { get; set; }
    public int msb { get; set; }
    public int lsb { get; set; }
    public LiteralType litType { get; set; }
    public CodeSequence(int LETTER_POS, int CURRENT_POS, int LEN, int ELIAS, LiteralType LT, int MSB, int LSB)
    {
        letterpos = LETTER_POS;
        currentpos = CURRENT_POS;
        len = LEN;
        elias = ELIAS;
        litType = LT;
        msb = MSB;
        lsb = LSB;
    }
    public CodeSequence(int LETTER_POS, int CURRENT_POS, int ELIAS) // literal
    {
        letterpos = LETTER_POS;
        currentpos = CURRENT_POS;
        len = 1;
        elias = ELIAS;
        litType = LiteralType.Literal;
        msb = 0;
        lsb = 0;
    }
    public CodeSequence(int LETTER_POS, int CURRENT_POS, int LEN, int ELIAS, LiteralType LT) // seq
    {
        letterpos = LETTER_POS;
        currentpos = CURRENT_POS;
        len = LEN;
        elias = ELIAS;
        litType = LT;
        msb = 0;
        lsb = 0;
    }
};
