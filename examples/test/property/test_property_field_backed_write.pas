program TestPropertyFieldBackedWrite;
type
    TPoint = class
    private
        FX: integer;
    public
        property X: integer read FX write FX;
    end;
var
    p: TPoint;

begin
    new(p);
    p.X := 42;
    writeln('X = ', p.X);
    dispose(p);
end.
