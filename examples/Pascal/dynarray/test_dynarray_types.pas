program TestDynArrayTypes;
var
    ints: array of integer;
    reals: array of real;
    chars: array of char;
    bools: array of boolean;
    bytes: array of byte;
    shorts: array of shortint;
    words: array of word;
    strs: array of string;
begin
    SetLength(ints, 2); ints[0] := 10; ints[1] := -5;
    SetLength(reals, 2); reals[0] := 1.5; reals[1] := 2.5;
    SetLength(chars, 2); chars[0] := 'x'; chars[1] := 'y';
    SetLength(bools, 2); bools[0] := true; bools[1] := false;
    SetLength(bytes, 2); bytes[0] := 200; bytes[1] := 255;
    SetLength(shorts, 2); shorts[0] := -100; shorts[1] := 100;
    SetLength(words, 2); words[0] := 60000; words[1] := 1;
    SetLength(strs, 2); strs[0] := 'hello'; strs[1] := 'world';

    writeln(ints[0], ' ', ints[1]);
    writeln(reals[0]:0:1, ' ', reals[1]:0:1);
    writeln(chars[0], chars[1]);
    writeln(bools[0], ' ', bools[1]);
    writeln(bytes[0], ' ', bytes[1]);
    writeln(shorts[0], ' ', shorts[1]);
    writeln(words[0], ' ', words[1]);
    writeln(strs[0], ' ', strs[1]);
end.
