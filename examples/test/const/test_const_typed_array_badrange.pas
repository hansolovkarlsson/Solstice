program TestConstTypedArrBadRange;
const
    { compiles fine (every element IS a compile-time constant) but
      fails at runtime, via the same subrange range-check ordinary
      byte-typed assignment already uses - 300 is out of byte's 0..255
      range }
    Bad: array[1..2] of byte = (1, 300);
begin
end.
