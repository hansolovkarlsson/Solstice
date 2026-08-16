program PropBadGetterIsProc;
type
    TCircle = class
    private
        FRadius: real;
    public
        procedure PrintRadius;
        property Radius: real read PrintRadius;
    end;
var
    c: TCircle;

procedure TCircle.PrintRadius;
begin
    writeln(FRadius:0:2);
end;

begin
    new(c);
    dispose(c);
end.
