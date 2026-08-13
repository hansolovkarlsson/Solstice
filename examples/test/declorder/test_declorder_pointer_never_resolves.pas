program TestDeclorderPtrNeverResolves;

{ A pointer type whose target genuinely never gets declared anywhere in
  the program (a typo/missing declaration, exactly like
  test_ptr_badforward.pas) must still produce the existing "targets
  undeclared type" error - confirms moving resolve_pending_pointer_types()
  to run once after the WHOLE repeated const/type/var loop (rather than
  at the end of each individual 'type' keyword block) didn't make a
  genuine error silently disappear, or get deferred somewhere it's never
  actually checked. A 'const' is interleaved in between to make sure
  that doesn't give the pending pointer any more chances to resolve than
  it should. }

type
    PFoo = ^TDoesNotExist;

const
    Unused = 1;

var
    p: PFoo;

begin
end.
