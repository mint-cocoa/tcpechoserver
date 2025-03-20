use std::env;
use std::io::{Read, Write};
use std::net::TcpStream;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{mpsc, Arc};
use std::thread;
use std::time::Duration;
use byteorder::{ByteOrder, LittleEndian};

// Message type definitions
const SERVER_ECHO: u8 = 0x05;
const CLIENT_CHAT: u8 = 0x13;
const PROTOCOL_VERSION: u8 = 0x01; // Protocol version
// Additional message types
const SERVER_ACK: u8 = 0x06;      // Server acknowledgment
const SERVER_INFO: u8 = 0x07;     // Server information
const CLIENT_INFO: u8 = 0x14;     // Client information

// Message constants
const MESSAGE_HEADER_SIZE: usize = 3; // type(1) + length(2)
const MESSAGE_DATA_SIZE: usize = 1021; // data size
const MESSAGE_TOTAL_SIZE: usize = MESSAGE_HEADER_SIZE + MESSAGE_DATA_SIZE; // total message size = 1024 bytes

fn print_usage(program: &str, opts: &getopts::Options) {
    let brief = format!(
        r#"Echo benchmark.

Usage:
  {program} [ -a <address> ] [ -l <length> ] [ -c <number> ] [ -t <duration> ]
  {program} (-h | --help)
  {program} --version"#,
        program = program
    );
    print!("{}", opts.usage(&brief));
}

struct Count {
    inb: u64,
    outb: u64,
}

// Chat message struct
struct ChatMessage {
    msg_type: u8,        // 1 byte
    length: u16,         // 2 bytes
    data: [u8; MESSAGE_DATA_SIZE],     // 1021 bytes fixed
}

impl ChatMessage {
    fn new(msg_type: u8, data: &[u8]) -> Self {
        let mut fixed_data = [0u8; MESSAGE_DATA_SIZE]; // 1021 bytes array initialized with 0
        let copy_len = std::cmp::min(data.len(), MESSAGE_DATA_SIZE);
        
        if copy_len > 0 {
            fixed_data[..copy_len].copy_from_slice(&data[..copy_len]);
        }
        
        ChatMessage {
            msg_type,
            length: copy_len as u16,
            data: fixed_data,
        }
    }

    // Message structure: type(1) + length(2) + data(1021) = always 1024 bytes
    fn pack(&self) -> Vec<u8> {
        let mut buffer = vec![0u8; MESSAGE_TOTAL_SIZE]; // 1 + 2 + 1021 = 1024 bytes
        
        // Type (1 byte)
        buffer[0] = self.msg_type;
        
        // Length (2 bytes)
        LittleEndian::write_u16(&mut buffer[1..3], self.length);
        
        // Data (1021 bytes)
        buffer[3..MESSAGE_TOTAL_SIZE].copy_from_slice(&self.data);
        
        buffer
    }

    fn unpack(data: &[u8]) -> Result<Self, &'static str> {
        if data.len() < MESSAGE_HEADER_SIZE {
            return Err("Incomplete message header");
        }
        
        let msg_type = data[0];
        let length = LittleEndian::read_u16(&data[1..3]);
        
        let mut fixed_data = [0u8; MESSAGE_DATA_SIZE];
        if data.len() >= MESSAGE_TOTAL_SIZE {
            fixed_data.copy_from_slice(&data[3..MESSAGE_TOTAL_SIZE]);
        } else if data.len() > 3 {
            // Copy as much as possible if data is less than 1024 bytes
            let available = data.len() - 3;
            fixed_data[..available].copy_from_slice(&data[3..3+available]);
        }
        
        Ok(ChatMessage {
            msg_type,
            length,
            data: fixed_data,
        })
    }
}

fn main() {
    let args: Vec<_> = env::args().collect();
    let program = args[0].clone();

    let mut opts = getopts::Options::new();
    opts.optflag("h", "help", "Print this help.");
    opts.optopt(
        "a",
        "address",
        "Target echo server address. Default: 127.0.0.1:12345",
        "<address>",
    );
    opts.optopt(
        "l",
        "length",
        "Test message length. Default: 1024",
        "<length>",
    );
    opts.optopt(
        "t",
        "duration",
        "Test duration in seconds. Default: 60",
        "<duration>",
    );
    opts.optopt(
        "c",
        "number",
        "Test connection number. Default: 50",
        "<number>",
    );

    let matches = match opts.parse(&args[1..]) {
        Ok(m) => m,
        Err(f) => {
            eprintln!("{}", f.to_string());
            print_usage(&program, &opts);
            return;
        }
    };

    if matches.opt_present("h") {
        print_usage(&program, &opts);
        return;
    }

    let length = matches
        .opt_str("length")
        .unwrap_or_default()
        .parse::<usize>()
        .unwrap_or(1024);
    let duration = matches
        .opt_str("duration")
        .unwrap_or_default()
        .parse::<u64>()
        .unwrap_or(60);
    let number = matches
        .opt_str("number")
        .unwrap_or_default()
        .parse::<u32>()
        .unwrap_or(10000);
    let address = matches
        .opt_str("address")
        .unwrap_or_else(|| "192.168.31.237:8080".to_string());

    let (tx, rx) = mpsc::channel();

    let stop = Arc::new(AtomicBool::new(false));
    let control = Arc::downgrade(&stop);

    for _ in 0..number {
        let tx = tx.clone();
        let address = address.clone();
        let stop = stop.clone();
        let length = length;

        thread::spawn(move || {
            let mut sum = Count { inb: 0, outb: 0 };
            // Generate test data
            let data_length = std::cmp::min(length, MESSAGE_DATA_SIZE);
            let test_data = vec![b'A'; data_length];
            let mut in_buf = vec![0u8; MESSAGE_TOTAL_SIZE]; // type(1) + length(2) + data(1021) = 1024 bytes
            
            let stream_result = TcpStream::connect(&*address);
            
            let mut stream = match stream_result {
                Ok(s) => {
                    // Set socket timeouts
                    s.set_read_timeout(Some(Duration::from_secs(5))).unwrap_or(());
                    s.set_write_timeout(Some(Duration::from_secs(5))).unwrap_or(());
                    s
                },
                Err(_) => {
                    return;
                }
            };
            
            loop {
                if (*stop).load(Ordering::Relaxed) {
                    break;
                }

                // Create and send CLIENT_CHAT message
                let message = ChatMessage::new(CLIENT_CHAT, &test_data);
                let out_buf = message.pack();
                
                match stream.write_all(&out_buf) {
                    Err(_) => {
                        break;
                    }
                    Ok(_) => {
                        sum.outb += 1;
                    }
                }

                if (*stop).load(Ordering::Relaxed) {
                    break;
                }

                // Receive response from server
                match stream.read(&mut in_buf) {
                    Err(_) => {
                        break;
                    },
                    Ok(m) => {
                        if m == 0 {
                            break;
                        }
                        
                        if m < MESSAGE_TOTAL_SIZE {
                            continue;
                        }
                        
                        // Parse response message
                        match ChatMessage::unpack(&in_buf) {
                            Ok(response) => {
                                // Accept more message types
                                if response.msg_type == SERVER_ECHO || 
                                   response.msg_type == SERVER_ACK || 
                                   response.msg_type == SERVER_INFO {
                                    // Process normal response
                                    sum.inb += 1;
                                }
                            },
                            Err(_) => {
                                // Continue trying
                            }
                        }
                    }
                };
            }
            tx.send(sum).unwrap();
        });
    }

    thread::sleep(Duration::from_secs(duration));

    match control.upgrade() {
        Some(stop) => (*stop).store(true, Ordering::Relaxed),
        None => println!("Sorry, but all threads died already."),
    }

    let mut sum = Count { inb: 0, outb: 0 };
    let mut received_responses = 0;
    
    for _ in 0..number {
        match rx.recv() {
            Ok(c) => {
                sum.inb += c.inb;
                sum.outb += c.outb;
                received_responses += 1;
            },
            Err(_) => {}
        }
    }
    
    println!("Benchmarking: {}", address);
    println!(
        "{} clients, running {} bytes, {} sec.",
        number, length, duration
    );
    println!();
    println!(
        "Speed: {} request/sec, {} response/sec",
        sum.outb / duration,
        sum.inb / duration
    );
    println!("Requests: {}", sum.outb);
    println!("Responses: {}", sum.inb);
    
    // Calculate success rate
    if sum.outb > 0 {
        let success_rate = (sum.inb as f64 / sum.outb as f64) * 100.0;
        println!("Success rate: {:.2}%", success_rate);
    }
}
