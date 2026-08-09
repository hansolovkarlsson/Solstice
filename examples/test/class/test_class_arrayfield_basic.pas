program TestClassArrayfieldBasic;
type
    TBuffer = class
        data: array[0..3] of integer;
        procedure Fill;
        function SumFirstTwo: integer;
    end;
var
    b: TBuffer;

procedure TBuffer.Fill;
begin
    { self-shorthand data[i] write inside a method }
    data[0] := 100;
    data[1] := 200;
    data[2] := 300;
    data[3] := 400;
end;

function TBuffer.SumFirstTwo;
begin
    { self-shorthand data[i] read inside a method }
    SumFirstTwo := data[0] + data[1];
end;

begin
    new(b);

    { explicit c.data[i] read/write }
    b.data[0] := 10;
    b.data[3] := 40;
    writeln('explicit data[0]: ', b.data[0]);
    writeln('explicit data[3]: ', b.data[3]);

    b.Fill;
    writeln('after Fill, data[1]: ', b.data[1]);
    writeln('after Fill, data[2]: ', b.data[2]);
    writeln('sum of first two: ', b.SumFirstTwo);

    dispose(b);
end.
