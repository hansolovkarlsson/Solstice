program TestNestedArray;

{ nested procedure indexes an enclosing local array directly - local
  arrays are already "hidden mangled globals" under the hood, so this
  should compile to ordinary array opcodes, not OP_LOAD_ENCLOSING/
  OP_STORE_ENCLOSING (verified separately via -v/desole) }
procedure Outer;
    var
        nums: array[1..5] of integer;
        sum: integer;

    procedure FillSquares;
        var i: integer;
    begin
        for i := 1 to 5 do
            nums[i] := i * i;
    end;

    procedure SumInto(var total: integer);
        var i: integer;
    begin
        total := 0;
        for i := 1 to 5 do
            total := total + nums[i];
    end;

begin
    FillSquares;
    SumInto(sum);
    writeln('sum=', sum);
end;

begin
    Outer;
end.
