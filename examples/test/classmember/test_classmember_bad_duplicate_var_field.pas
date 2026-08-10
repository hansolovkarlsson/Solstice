program TestCMBadDupVarField;
type
    TCounter = class
    public
        Total: integer;
        class var Total: integer;
    end;

begin
end.
