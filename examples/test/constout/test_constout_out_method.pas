program TestConstoutOutMethod;
type
    TCalc = class
        procedure Compute(const a: integer; out b: integer);
    end;
var
    c: TCalc;
    input, result: integer;

procedure TCalc.Compute;
begin
    b := a * 2;
end;

begin
    new(c);
    input := 21;
    c.Compute(input, result);
    writeln(result);
    dispose(c);
end.
