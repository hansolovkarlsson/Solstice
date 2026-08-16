program TestCMBadCtor;
type
    TCounter = class
    public
        class procedure Init;
    end;
var c: TCounter;

procedure TCounter.Init;
begin
end;

begin
    new(c, Init());
end.
