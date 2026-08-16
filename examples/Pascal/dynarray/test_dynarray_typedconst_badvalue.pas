program TestDArrTCBadValue;
{ A BARE dynamic-array typed constant's value must be an array literal
  ('[...]') - nothing else, including 'nil', is accepted. }
const
    X: array of integer = nil;
begin
end.
