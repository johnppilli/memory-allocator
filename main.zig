

const std = @import("std");

pub fn main() void {
    std.debug.print("memory alocator\n", .{};
}


const Blcok = struct {
    size: usize, 
    is_free: bool, 
};






