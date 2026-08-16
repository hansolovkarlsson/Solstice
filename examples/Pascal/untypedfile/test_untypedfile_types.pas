program TestUntypedfileTypes;

{ One array per typed-file-safe element type (integer, real, boolean,
  an enumerated type, an ordinary subrange) round-tripped through
  BlockWrite/BlockRead. Byte/shortint/word arrays are a documented v1
  gap (see docs/LANGUAGE.md) - not tested here.
  42 3.5 TRUE Green 7 }

type
    TColor = (Red, Green, Blue);
    TSmall = 1..10;

var
    f: file;
    ints: array[0..0] of integer;
    reals: array[0..0] of real;
    bools: array[0..0] of boolean;
    colors: array[0..0] of TColor;
    subs: array[0..0] of TSmall;

begin
    ints[0] := 42;
    reals[0] := 3.5;
    bools[0] := true;
    colors[0] := Green;
    subs[0] := 7;

    assign(f, '/tmp/untypedfile_types.bin');
    rewrite(f);
    BlockWrite(f, ints, 1);
    BlockWrite(f, reals, 1);
    BlockWrite(f, bools, 1);
    BlockWrite(f, colors, 1);
    BlockWrite(f, subs, 1);
    close(f);

    ints[0] := 0;
    reals[0] := 0;
    bools[0] := false;
    colors[0] := Red;
    subs[0] := 1;

    reset(f);
    BlockRead(f, ints, 1);
    BlockRead(f, reals, 1);
    BlockRead(f, bools, 1);
    BlockRead(f, colors, 1);
    BlockRead(f, subs, 1);
    close(f);

    write(ints[0], ' ', reals[0]:0:1, ' ');
    if bools[0] then write('TRUE ') else write('FALSE ');
    if colors[0] = Green then write('Green ') else write('other ');
    writeln(subs[0]);
end.
