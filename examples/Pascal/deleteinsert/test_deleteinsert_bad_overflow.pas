program TestDeleteinsertBadOverflow;
var
    s: string;
    big: string;
    i: integer;
begin
    s := '';
    for i := 1 to 60 do
        s := s + 'abcd';
    big := s;
    Insert(big, s, 1);
end.
