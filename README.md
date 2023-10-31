# scr2atari
Convert monochrome ZX SCR art to ATARI XL/XE binary viewer, create B/W and colored PNG file preview

For ATARI compilation use MADS Assembler https://mads.atari8.info/

C# project compiled on VS .NET 4.8

Command line usage:<br>
scr2asm [-option1][-option2] [filename1] [*.scr] - filenames or masks separated by space<br>
        -pngbw  : only generate monochrome PNG files<br>
        -png    : only generate color PNG files<br>
        -png2   : only generate color PNG files, scale x2<br>
        -png4   : only generate color PNG files, scale x4<br>
        -noobx  : not  generate OBX files (only ASM and PNG B/W)<br>
        -nopng  : not  generate PNG files (only ASM and OBX)<br>
