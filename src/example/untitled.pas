{ test for multi-level and display region disign}
var vn, out, i: integer;
	A:array[10] of integer;
procedure outter(y:integer);
	procedure writeoo();
	begin
		write(2222)
	end;
	procedure foo(y:integer);
		procedure pcd(x:integer);
			var t: integer;
			function sum(var x:integer):integer;
				var t: integer;
				begin
					t := x-1;
					if x = 1 then 
						sum := 1
					else
						sum := sum(t)+x;
				end;
			begin
				t := x;
				out := sum(t)
			end;
		begin
			pcd(y);
			writeoo()
		end;
	begin
		foo(y)
	end;
begin
	A[0] := 32;
	A[1] := 2;
	A[2] := 128;
	A[3] := 32;
	write(A[0]/A[1]);
	write(A[1]*A[3]);
	for i := 0 to 3 do
	begin
		write(A[i])
	end;
	vn := 100;
	outter(vn);
	write(out) {expect output a number: 5050}
end.

