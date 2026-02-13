

const std = @import("std"); //import Zig library

pub fn main() void {  //entry point of program, like main() in C
    std.debug.print("memory alocator\n", .{}; //this will just print this text to the terminal
}


const Blcok = struct {
    size: usize, //stores how big the memory block is, unsigned integer
    is_free: bool, //tracks whether this block is available or taken
};






