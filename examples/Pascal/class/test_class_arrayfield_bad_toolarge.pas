program TestClassArrfieldTooLarge;
type
    TOverflow = class
        { flattened heap footprint: 1 (hidden tag) + 20 (array) = 21,
          past the MAX_RECORD_FIELDS+1 = 17 heap-slot limit every class
          instance's allocation size class must fit. Expected: Compile
          Error - must be rejected at compile time, not silently
          overflow the VM's heap freelist. }
        big: array[0..19] of integer;
    end;
var
    o: TOverflow;
begin
    new(o);
    dispose(o);
end.
