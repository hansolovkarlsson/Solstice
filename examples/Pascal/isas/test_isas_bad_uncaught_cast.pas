program TestIsasBadUncaught;
type
    TShape = class
    public
        x: integer;
    end;
    TCircle = class(TShape)
    public
        y: integer;
    end;
var
    shape: TShape;
    c: TCircle;
begin
    new(shape);
    { no enclosing try - the failed cast is unhandled }
    c := shape as TCircle;
    writeln('BUG: should not reach here');
    dispose(shape);
end.
