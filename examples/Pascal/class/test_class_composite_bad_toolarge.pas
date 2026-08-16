program TestClassCompositeBadToolarge;
type
    { 16 scalar fields - exactly MAX_RECORD_FIELDS, the largest a plain
      record can be. }
    TBig = record
        f1, f2, f3, f4, f5, f6, f7, f8: integer;
        f9, f10, f11, f12, f13, f14, f15, f16: integer;
    end;
    TOverflow = class
        { flattened heap footprint: 1 (hidden tag) + 16 (TBig's leaves)
          + 1 (extra) = 18, past the MAX_RECORD_FIELDS+1 = 17 heap-slot
          limit every class instance's allocation size class must fit.
          Expected: Compile Error - must be rejected at compile time,
          not silently overflow the VM's heap freelist. }
        big: TBig;
        extra: integer;
    end;
var
    o: TOverflow;
begin
    new(o);
    dispose(o);
end.
