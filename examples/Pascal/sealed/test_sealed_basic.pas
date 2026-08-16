program TestSealedBasic;
type
    TFoo = class sealed
        x: integer;
        function GetX: integer;
    end;
var
    f: TFoo;

function TFoo.GetX;
begin
    GetX := x;
end;

begin
    new(f);
    f.x := 42;
    writeln(f.GetX);
    dispose(f);
end.
