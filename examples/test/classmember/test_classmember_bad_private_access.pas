program TestCMBadPrivate;
type
    TCounter = class
    private
        class var Total: integer;
    end;

begin
    TCounter.Total := 5;
end.
