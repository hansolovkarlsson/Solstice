program TestDeclorderConstNamedElemtype;

{ Before interleaving, a named enum/subrange element type could never
  resolve inside a typed constant's array element type anyway, since
  'type' always parsed after 'const' - the restriction was purely a side
  effect of ordering, not an explicit check. Now that a 'type' declared
  EARLIER in the source is visible here, this must still be rejected by
  its own explicit check (see parse_typed_const_declaration() in
  parser.c) - confirms interleaving didn't silently and untestedly widen
  what's accepted as a typed constant's array element type. }

type
    TColor = (Red, Green, Blue);

const
    Colors: array[1..3] of TColor = (Red, Green, Blue);

begin
end.
