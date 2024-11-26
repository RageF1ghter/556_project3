import random

def generate_test_case(nodes, links, duration, interval, output_file):
    output = []
    # Header
    output.append("[nodes]\n")
    output.append(" ".join(map(str, nodes)) + "\n\n")
    output.append("[links]\n")
    for link in links:
        output.append(f"({link[0]},{link[1]}) delay {link[2]:.3f} prob {link[3]:.1f}\n")
    output.append("\n[events]\n")

    # Generate events
    time = 0.00
    while time <= duration:
        node1, node2 = random.sample(nodes, 2)  # Select two random distinct nodes
        output.append(f"{time:.2f} xmit ({node1},{node2})\n")
        time += interval

    output.append(f"{time - interval:.2f} end\n")  # Ensure the end is marked after the last interval

    # Write to the file
    with open(output_file, "w") as file:
        file.writelines(output)

    print(f"Test case written to {output_file}")

# Define parameters
nodes = [1, 2, 3, 4, 5, 6]
links = [
    (1, 2, 0.010, 0.0),
    (1, 3, 0.025, 0.0),
    (1, 4, 0.005, 0.0),
    (2, 3, 0.015, 0.0),
    (2, 4, 0.010, 0.0),
    (3, 4, 0.015, 0.0),
    (3, 5, 0.005, 0.0),
    (3, 6, 0.025, 0.0),
    (4, 5, 0.005, 0.0),
    (5, 6, 0.010, 0.0),
]
duration = 300000  # 30,000 seconds
interval = 100  # 100 seconds
output_file = "test5"  # Output file

# Generate and write the test case
generate_test_case(nodes, links, duration, interval, output_file)
