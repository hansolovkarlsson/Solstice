program TestPtrBadforward;
{ A pointer type forward-referencing a name that's never actually
  declared anywhere in the same 'type' section (a typo, or a genuinely
  missing declaration) must be a clear Compile Error, not a crash or a
  silently wrong resolution. }
type
    PFoo = ^TDoesNotExist;
var
    p: PFoo;
begin
end.
