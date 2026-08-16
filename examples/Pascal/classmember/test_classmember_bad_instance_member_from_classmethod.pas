program TestCMBadInstFromCM;
type
    TCounter = class
    public
        FVal: integer;
        class procedure Bad;
    end;

procedure TCounter.Bad;
begin
    FVal := 1;
end;

begin
    TCounter.Bad;
end.
