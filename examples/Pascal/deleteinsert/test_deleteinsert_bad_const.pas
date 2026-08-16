program TestDeleteinsertBadConst;
procedure P(const s: string);
begin
    Delete(s, 1, 1);
end;
begin
    P('abc');
end.
