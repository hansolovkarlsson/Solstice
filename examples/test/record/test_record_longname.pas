program TestRecordLongName;
type TVeryLongRecordTypeNameIndeed = record thisFieldNameIsAlsoRatherLong: integer; end;
var thisVariableNameIsQuiteLongToo: TVeryLongRecordTypeNameIndeed;
begin
    thisVariableNameIsQuiteLongToo.thisFieldNameIsAlsoRatherLong := 1;
end.
