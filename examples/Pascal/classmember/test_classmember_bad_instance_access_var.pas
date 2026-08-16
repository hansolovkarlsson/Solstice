program TestCMBadInstVar;
type
    TCounter = class
    public
        class var Total: integer;
    end;
var c: TCounter;

begin
    new(c);
    writeln(c.Total);
end.
