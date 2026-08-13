program TestConstTypedArrBadNonConst;
const
    { string concatenation doesn't fold to a literal in this compiler
      (see test_const_badconcat.pas) - the same "not a compile-time
      constant" rule applies to a typed constant's own elements }
    Greetings: array[1..1] of string = ('Hello' + ', World');
begin
end.
