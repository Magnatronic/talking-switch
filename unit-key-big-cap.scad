// =====================================================================
//  Guided big-cap accessibility switch for M5Stack Unit Key (U144)
// ---------------------------------------------------------------------
//  - Large press surface on the Unit Key's MX-style stem
//  - Printed collar guides the cap's skirt, so off-centre presses
//    still travel straight down (no tipping or binding)
//  - Stop ledge limits travel to protect the switch
//  - Pocket for the unit, exit slot for the Grove cable
//  - Captive 1/4"-20 nut for standard AT mounting arms / camera clamps
//  - Optional magnet pockets, symbol recess, tactile textures
//  - Optional side cradle for an M5StickS3 (talking switch / HID / IR brain)
//
//  PRINTING
//    Base: flat side down, no supports.
//    Cap:  printed top-face-down (already flipped in "all" / "cap"),
//          no supports. Translucent PETG lets the RGB LED glow through.
//
//  ASSEMBLY
//    1. Push a 1/4"-20 hex nut into the hex hole in the pocket floor.
//    2. Press-fit magnets (with a dab of glue) into the underside.
//    3. Drop the Unit Key into the pocket, cable out through the slot.
//    4. Plug the Grove cable into the StickS3 and seat it in the cradle,
//       screen up, Grove end towards the switch. Coil spare cable in the
//       bay (a 10cm Grove cable is tidier than the supplied 20cm one).
//    5. Remove the stock keycap and push the big cap onto the stem.
//
//  !! MEASURE YOUR UNIT FIRST and set the "Measure these" values. !!
//  Print "stem_test" first to dial in the stem fit (stem_tol).
// =====================================================================

/* [Part to render] */
part = "all"; // [all, base, cap, assembly, stem_test]

/* [Measure these on your unit] */
// Unit housing length (mm)
unit_l = 40;
// Unit housing width (mm)
unit_w = 24;
// Height from the bottom of the unit to the top of the MX stem, stock keycap REMOVED (mm)
stem_top_h = 18;
// Offset of the switch centre from the unit's centre along its length (+ = away from the Grove connector end)
key_x_offset = 8;
// Clearance around the unit in its pocket (mm)
unit_clearance = 0.3;

/* [Cap] */
// Diameter of the press surface (mm)
cap_d = 72;
// Thickness of the top plate (mm)
top_t = 4;
// Skirt wall thickness (mm)
skirt_t = 2;
// Radial gap between skirt and collar (mm). Increase if it rubs.
fit_clearance = 0.4;
// How far the skirt sits inside the collar at rest (mm). More = steadier.
engage = 10;
// Maximum travel before the skirt hits the stop ledge (mm). Blue switches actuate ~2mm and bottom out ~4mm.
stop_travel = 3.5;

/* [Stem socket] */
// Length of each arm of the MX cross (mm)
cross_len = 4.1;
// Width of the MX cross arms (mm)
cross_w = 1.3;
// Extra clearance on the cross (mm). Tune with the stem_test part.
stem_tol = 0.1;
// Depth of the stem socket (mm)
socket_depth = 3.8;
// Diameter of the boss around the socket (mm)
boss_d = 5.6;

/* [Top surface] */
// Shallow recess for a paper symbol under a clear disc
symbol_recess = true;
// Symbol recess diameter (mm)
symbol_d = 50;
// Symbol recess depth (mm): paper + ~0.5mm clear acetate/PETG disc
symbol_depth = 1.2;
// Tactile texture (applied outside the symbol area)
texture = "none"; // [none, grooves, dimples]

/* [Base] */
// Overall base diameter (mm). Wider = more stable.
base_d = 90;
// Solid floor under the unit pocket (mm). Must be > nut_t + ~2.
floor_t = 8;
// How deep the unit sits in the base (mm)
pocket_depth = 6;
// Collar wall thickness (mm)
wall_t = 3;
// Grove cable slot width (mm)
cable_w = 11;
// Grove cable slot height (mm)
cable_h = 7;
// Captive 1/4"-20 nut for mounting arms
nut_trap = true;
// Nut across-flats (mm). 1/4"-20 hex nut = 11.11
nut_af = 11.11;
// Nut thickness allowance (mm)
nut_t = 5.8;
// Clearance hole for the 1/4" stud (mm)
bolt_d = 6.6;
// Magnet pockets on the underside
magnets = true;
// Magnet diameter (mm)
magnet_d = 10;
// Magnet thickness (mm)
magnet_t = 3;
// Magnet pocket clearance (mm)
magnet_tol = 0.2;

/* [StickS3 cradle] */
// Add a side cradle that holds an M5StickS3, screen up, Grove end facing the switch
stick_cradle = true;
// StickS3 length (mm)
stick_l = 48;
// StickS3 width (mm)
stick_w = 24;
// StickS3 thickness (mm)
stick_t = 15;
// Clearance around the StickS3 (mm)
stick_clearance = 0.4;
// How deep the StickS3 sits in the cradle (mm). Keep low so side buttons stay reachable.
stick_pocket_depth = 6;
// Space between the cradle and the switch for the Grove plug and spare cable (mm)
cable_bay_l = 14;
// Cradle wall thickness (mm)
cradle_wall = 3;
// Holes under the StickS3 so its speaker is not muffled
speaker_holes = true;

/* [Hidden] */
$fn = 96;
eps = 0.01;

// ---------------------------------------------------------------------
//  Derived geometry (world Z = 0 at the bottom of the base)
// ---------------------------------------------------------------------
cap_r          = cap_d / 2;
collar_ir      = cap_r + fit_clearance;
collar_or      = collar_ir + wall_t;
base_h         = floor_t + pocket_depth;
stem_top_z     = floor_t + stem_top_h;
boss_bottom_z  = stem_top_z - socket_depth;   // underside of cap boss at rest
skirt_bottom_z = base_h + stop_travel + 1;    // 1mm spare gap at full stop
plate_under_z  = skirt_bottom_z + engage;     // underside of top plate at rest
boss_len       = plate_under_z - boss_bottom_z;
stop_z         = skirt_bottom_z - stop_travel; // top of the stop ledge
ledge_ir       = cap_r - skirt_t - 1;
unit_corner_r  = sqrt(pow(unit_l/2 + abs(key_x_offset) + unit_clearance, 2)
                    + pow(unit_w/2 + unit_clearance, 2));

// StickS3 cradle layout (along -X, the same side as the cable slot)
stick_pl       = stick_l + 2 * stick_clearance;  // pocket length
stick_pw       = stick_w + 2 * stick_clearance;  // pocket width
cradle_in_x    = -base_d / 2 + 3;                // inner end of the cable bay
stick_in_x     = cradle_in_x - cable_bay_l;      // StickS3 end nearest the switch
stick_out_x    = stick_in_x - stick_pl;          // StickS3 outer (USB-C) end
cradle_out_x   = stick_out_x - cradle_wall;
cradle_floor_z = base_h - stick_pocket_depth;
cradle_half_w  = stick_pw / 2 + cradle_wall;

assert(!stick_cradle || cradle_in_x < -collar_or,
       "Cable bay cuts into the collar: increase base_d.");
assert(!stick_cradle || cradle_floor_z >= 2,
       "Cradle floor too thin: reduce stick_pocket_depth.");

assert(boss_len >= socket_depth + 0.5,
       "Stem top is above the cap plate: increase engage or reduce pocket_depth.");
assert(unit_corner_r + 0.5 < ledge_ir,
       "Unit does not fit inside the skirt: increase cap_d.");
assert(collar_or < base_d / 2,
       "Collar is wider than the base: increase base_d.");
assert(!nut_trap || floor_t >= nut_t + 1.5,
       "Floor too thin for the nut: increase floor_t.");

echo(str("Base height: ", base_h, " mm, collar top: ", plate_under_z, " mm"));
echo(str("Cap stands ", top_t, " mm proud of the collar at rest, ",
         top_t - stop_travel, " mm at full press"));
echo(str("Max 1/4\" stud length into base: ~", floor_t, " mm"));

// ---------------------------------------------------------------------
//  Modules
// ---------------------------------------------------------------------
module mx_socket() {
    l = cross_len + 2 * stem_tol;
    w = cross_w + 2 * stem_tol;
    linear_extrude(socket_depth + eps) {
        square([l, w], center = true);
        square([w, l], center = true);
    }
    // Lead-in chamfer so the cap finds the stem easily
    translate([0, 0, -eps]) cylinder(d1 = l + 0.8, d2 = l, h = 0.4);
}

module texture_cuts(z_top) {
    r_in = symbol_recess ? symbol_d / 2 + 4 : 6;
    r_out = cap_r - 4;
    if (texture == "grooves") {
        for (r = [r_in : 6 : r_out])
            translate([0, 0, z_top - 1])
                difference() {
                    cylinder(r = r + 0.8, h = 1 + eps);
                    translate([0, 0, -1]) cylinder(r = r - 0.8, h = 3);
                }
    }
    if (texture == "dimples") {
        for (x = [-r_out : 7 : r_out], y = [-r_out : 7 : r_out]) {
            d = sqrt(x * x + y * y);
            if (d >= r_in && d <= r_out)
                translate([x, y, z_top + 0.8]) sphere(d = 4, $fn = 24);
        }
    }
}

// Cap in local coordinates: Z = 0 is the bottom of the stem boss.
module cap() {
    plate_z = boss_len;
    skirt_z = skirt_bottom_z - boss_bottom_z;
    difference() {
        union() {
            // Top plate
            translate([0, 0, plate_z]) cylinder(r = cap_r, h = top_t);
            // Skirt (guided by the collar)
            translate([0, 0, skirt_z])
                difference() {
                    cylinder(r = cap_r, h = plate_z - skirt_z + eps);
                    translate([0, 0, -1])
                        cylinder(r = cap_r - skirt_t, h = plate_z - skirt_z + 2);
                }
            // Stem boss with a small flare into the plate
            cylinder(d = boss_d, h = plate_z + eps);
            translate([0, 0, plate_z - 2])
                cylinder(d1 = boss_d, d2 = boss_d + 4, h = 2 + eps);
        }
        mx_socket();
        if (symbol_recess)
            translate([0, 0, plate_z + top_t - symbol_depth])
                cylinder(d = symbol_d, h = symbol_depth + eps);
        texture_cuts(plate_z + top_t);
    }
}

// ---------------------------------------------------------------------
//  StickS3 cradle
// ---------------------------------------------------------------------
module cradle_block() {
    // Rounded outer end, flat sides, merges into the base cylinder
    hull() {
        translate([cradle_in_x + 5, -cradle_half_w, 0])
            cube([1, 2 * cradle_half_w, base_h]);
        for (y = [-1, 1])
            translate([cradle_out_x + 4, y * (cradle_half_w - 4), 0])
                cylinder(r = 4, h = base_h);
    }
}

module cradle_cuts() {
    // StickS3 pocket
    translate([stick_out_x, -stick_pw / 2, cradle_floor_z])
        cube([stick_pl, stick_pw, base_h]);
    // Cable bay for the Grove plug and spare cable
    translate([stick_in_x - eps, -stick_pw / 2, cradle_floor_z])
        cube([cable_bay_l + 2 * eps, stick_pw, base_h]);
    // USB-C access at the outer end
    translate([cradle_out_x - 1, -7, cradle_floor_z + 2])
        cube([cradle_wall + 2, 14, base_h]);
    // Finger notches on both sides to lift the StickS3 out
    for (y = [-1, 1])
        translate([stick_out_x + stick_pl / 2, y * (cradle_half_w), cradle_floor_z + 2])
            scale([1.4, 1, 1]) cylinder(r = 7, h = base_h);
    // Speaker holes through the floor
    if (speaker_holes)
        for (x = [stick_out_x + 8 : 6 : stick_in_x - 6], y = [-6, 0, 6])
            translate([x, y, -eps]) cylinder(d = 3, h = cradle_floor_z + 2 * eps, $fn = 16);
}

module base() {
    difference() {
        union() {
            cylinder(r = base_d / 2, h = base_h);
            cylinder(r = collar_or, h = plate_under_z);
            if (stick_cradle) cradle_block();
        }
        if (stick_cradle) cradle_cuts();
        // Collar bore above the stop ledge
        translate([0, 0, stop_z]) cylinder(r = collar_ir, h = plate_under_z);
        // Clear the centre down to the base top, leaving the stop ledge ring
        translate([0, 0, base_h]) cylinder(r = ledge_ir, h = plate_under_z);
        // Unit pocket (unit centre offset so the switch sits on the axis)
        translate([-key_x_offset, 0, floor_t + (pocket_depth + 20) / 2])
            cube([unit_l + 2 * unit_clearance,
                  unit_w + 2 * unit_clearance,
                  pocket_depth + 20], center = true);
        // Grove cable slot out through the -X side. It stops below the
        // stop ledge so the collar stays a complete guide ring.
        slot_end = -key_x_offset - unit_l / 2 + 1;
        translate([-base_d / 2 - 1, -cable_w / 2, floor_t])
            cube([slot_end + base_d / 2 + 1, cable_w, min(cable_h, stop_z - floor_t)]);
        // Captive 1/4"-20 nut, dropped in from the pocket side
        if (nut_trap) {
            translate([0, 0, floor_t - nut_t])
                cylinder(r = (nut_af / 2) / cos(30) + 0.15, h = nut_t + eps, $fn = 6);
            translate([0, 0, -eps]) cylinder(d = bolt_d, h = floor_t);
        }
        // Magnet pockets, clear of the cable slot
        if (magnets)
            for (a = [0, 90, 270])
                rotate([0, 0, a])
                    translate([base_d / 2 - magnet_d / 2 - 4, 0, -eps])
                        cylinder(d = magnet_d + magnet_tol, h = magnet_t + magnet_tol);
    }
}

// Small test piece: a stub with the socket, to check stem grip quickly.
module stem_test() {
    for (i = [0 : 3]) {
        translate([i * 10, 0, 0])
            difference() {
                cylinder(d = 8, h = socket_depth + 1.5);
                translate([0, 0, -eps])
                    let(t = stem_tol - 0.05 + i * 0.05)
                        linear_extrude(socket_depth) {
                            square([cross_len + 2 * t, cross_w + 2 * t], center = true);
                            square([cross_w + 2 * t, cross_len + 2 * t], center = true);
                        }
            }
    }
    echo(str("stem_test tolerances left to right: ",
             [for (i = [0 : 3]) stem_tol - 0.05 + i * 0.05]));
}

// ---------------------------------------------------------------------
//  Output
// ---------------------------------------------------------------------
cap_print_h = boss_len + top_t;

if (part == "base") base();

if (part == "cap")
    translate([0, 0, cap_print_h]) rotate([180, 0, 0]) cap();

if (part == "all") {
    base();
    translate([base_d / 2 + cap_r + 10, 0, cap_print_h])
        rotate([180, 0, 0]) cap();
}

if (part == "assembly") {
    color("lightgray") base();
    color("orange", 0.8) translate([0, 0, boss_bottom_z]) cap();
    // Ghost of the Unit Key
    %translate([-key_x_offset, 0, floor_t + stem_top_h / 2])
        cube([unit_l, unit_w, stem_top_h], center = true);
    // Ghost of the StickS3
    if (stick_cradle)
        %translate([stick_out_x + stick_clearance, -stick_w / 2, cradle_floor_z])
            cube([stick_l, stick_w, stick_t]);
}

if (part == "stem_test") stem_test();
