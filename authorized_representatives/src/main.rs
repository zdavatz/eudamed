use std::env;
use std::error::Error;
use std::fs::File;
use std::io::Write;
use serde_json::{Value, from_reader};
use csv::Writer;

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        eprintln!("Usage: {} <input.json> [output.csv]", args[0]);
        eprintln!("  - <input.json>   : Path to the input JSON file (required)");
        eprintln!("  - [output.csv]   : Path to the output CSV file (optional)");
        eprintln!("                     If omitted, output will be '<input>.csv'");
        std::process::exit(1);
    }

    let input_path = args[1].clone();

    let output_path = if args.len() >= 3 {
        args[2].clone()
    } else {
        if input_path.ends_with(".json") {
            input_path.replace(".json", ".csv")
        } else {
            format!("{}.csv", input_path)
        }
    };

    let input_file = File::open(&input_path)
        .map_err(|e| format!("Could not open input file '{}': {}", input_path, e))?;

    let data: Vec<Value> = from_reader(input_file)?;

    // Open output file and manually write UTF-8 BOM for Excel compatibility
    let mut output_file = File::create(&output_path)?;
    output_file.write_all(b"\xEF\xBB\xBF")?; // UTF-8 BOM

    let mut wtr = Writer::from_writer(output_file);

    // Headers in consistent order
    let headers = vec![
        "uuid",
        "ulid",
        "name",
        "names",
        "actorType",
        "actorStatus",
        "actorStatusFromDate",
        "roleName",
        "countryIso2Code",
        "countryName",
        "countryType",
        "dateOfRegistration",
        "eudamedIdentifier",
        "electronicMail",
        "telephone",
        "geographicalAddress",
        "buildingNumber",
        "streetName",
        "postbox",
        "addressComplement",
        "postalZone",
        "cityName",
        "abbreviatedName",
        "abbreviatedNames",
        "latestVersion",
        "versionNumber",
        "associatedToUser",
        "registrationUlid",
        "legislationLinks",
        "selectable",
        "actorValidated",
        "srn",
    ];

    wtr.write_record(&headers)?;

    let row_count = data.len();

    for item in data {
        if let Value::Object(map) = item {
            let mut row: Vec<String> = Vec::new();

            for key in &headers {
                let val = map.get(*key).unwrap_or(&Value::Null);

                let cell = match val {
                    Value::String(s) => s.clone(),
                    Value::Null => "".to_string(), // empty for null (optional – change to "null" if you prefer)
                    _ => val.to_string(),
                };

                row.push(cell);
            }

            wtr.write_record(&row)?;
        }
    }

    wtr.flush()?;
    println!("Successfully converted '{}' → '{}' ({} data rows + header)", input_path, output_path, row_count);
    println!("NOTE: UTF-8 BOM added for proper Excel display of umlauts (ä, ö, ü, ß, etc.)");

    Ok(())
}
