// benchmark/bench.rs — sum loop 0..20,000,000
fn main() {
    let n: i64 = 20_000_000;
    let mut i: i64 = 0;
    let mut sum: i64 = 0;
    while i < n { sum += i; i += 1; }
    println!("{}", sum);
}
