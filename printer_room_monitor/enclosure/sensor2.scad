wall = 1.6;

m2 = 1.9;
m3 = 2.76;

proto = [50.8, 38.1, wall]; // electrocookie mini
inner_dims = [64,45];

case(30);
translate([0, inner_dims.y + 20, 0]) cover(8);


module cover(height) {
    difference() {
        union() {
            difference() {
                rcube([inner_dims.x + 2*wall, inner_dims.y + 2*wall, height+5], 2);
                translate([-0.01, -0.01, height]) cube([inner_dims.x + 2*wall+1, inner_dims.y + 2*wall+1, height]);
                translate([wall, wall, wall]) cube([inner_dims.x, inner_dims.y, height]);

                // cutouts for screws
                translate([4.5,4.5,0]) cylinder(d=6, h=height);
                translate([inner_dims.x+2*wall-4.5,4.5,0]) cylinder(d=6, h=height);
                translate([inner_dims.x+2*wall-4.5,inner_dims.y+2*wall-4.5,0]) cylinder(d=6, h=height);
                translate([4.5,inner_dims.y+2*wall-4.5,0]) cylinder(d=6, h=height);
            }
            
            // screws for the cover
            translate([4.5,4.5,0]) post(8, m3, height, height);
            translate([inner_dims.x+2*wall-4.5,4.5,0]) post(8, m3, height, height);
            translate([inner_dims.x+2*wall-4.5,inner_dims.y+2*wall-4.5,0]) post(8, m3, height, height);
            translate([4.5,inner_dims.y+2*wall-4.5,0]) post(8, m3, height, height);  
            
            // alignment tabs
            translate([wall+(inner_dims.x-50)/2,wall,wall]) cube([50,2,height]);
            translate([wall+(inner_dims.x-50)/2,inner_dims.y+wall-2,wall]) cube([50,2,height]);
        }
        
        // screw head opening
        head=[6,2.6];
        $fn=32;
        translate([4.5,4.5,-0.01]) cylinder(d=head.x,h=head.y);
        translate([inner_dims.x+2*wall-4.5,4.5,-0.01]) cylinder(d=head.x,h=head.y);
        translate([inner_dims.x+2*wall-4.5,inner_dims.y+2*wall-4.5,-0.01]) cylinder(d=head.x,h=head.y);
        translate([4.5,inner_dims.y+2*wall-4.5,-0.01]) cylinder(d=head.x,h=head.y);

        // breathing slits
        dia = 2.8;
        wp = .6;
        translate([8,wall+inner_dims.y/2,0]) slit(dia, inner_dims.y*wp/2);
        translate([50,wall+inner_dims.y*(1-wp)/2,0]) slit(dia, inner_dims.y*wp/2);
        for (i = [1 : 4]) {
            translate([i*10,wall+inner_dims.y*(1-wp)/2,0]) slit(dia, inner_dims.y*wp);
        }

    }
}

module case(height) {
    difference() {
        union() {
            difference() {
                rcube([inner_dims.x + 2*wall, inner_dims.y + 2*wall, height+5], 2);
                translate([-0.01, -0.01, height]) cube([inner_dims.x + 2*wall+1, inner_dims.y + 2*wall+1, height]);
                translate([wall, wall, wall]) cube([inner_dims.x, inner_dims.y, height]);

                // cutouts for screws
                translate([4.5,4.5,wall]) cylinder(d=6, h=height);
                translate([inner_dims.x+2*wall-4.5,4.5,wall]) cylinder(d=6, h=height);
                translate([inner_dims.x+2*wall-4.5,inner_dims.y+2*wall-4.5,wall]) cylinder(d=6, h=height);
                translate([4.5,inner_dims.y+2*wall-4.5,wall]) cylinder(d=6, h=height);
            }
            
            // screws for the cover
            translate([4.5,4.5,0]) post(6, m3, height, height);
            translate([inner_dims.x+2*wall-4.5,4.5,0]) post(6, m3, height, height);
            translate([inner_dims.x+2*wall-4.5,inner_dims.y+2*wall-4.5,0]) post(6, m3, height, height);
            translate([4.5,inner_dims.y+2*wall-4.5,0]) post(6, m3, height, height);  
            
            // screw mounts for protoboard
            translate([wall+(inner_dims.x-proto.x)/2, wall+(inner_dims.y-proto.y)/2, 0]) mount();
        }
        
        // cutout for usb for programming
        translate([inner_dims.x+wall-0.01, inner_dims.y/2-10+wall,wall+3]) rounded_cutout(10,20,5,[90,0,90]);
        
        // cutout for air vents
        for (i = [0 : 4]) {
            translate([16 + 10*i,-0.01,24]) rounded_cutout(6,16,inner_dims.y+2*wall+1,[0,90,90]);
        }
        for (i = [0 : 2]) {
            translate([-1,10*i+10,24]) rounded_cutout(6,16,5,[90,90,90]);
        }
        for (i = [0 : 2]) {
            translate([wall+inner_dims.x-0.01,1+10*i+10,26]) rounded_cutout(6,8,5,[90,90,90]);
        }
    }  
}

module rounded_cutout(height, width, thick, rot = [0,0,0]) {
    $fn = 24;
    rotate(rot) translate([height/2, height/2, 0]) union() {
        cylinder(d=height, h=thick);
        translate([0, -height/2, 0]) cube([width-height, height, thick]);
        translate([width-height, 0, 0]) cylinder(d=height, h=thick);        
    }
}

module slit(dia, w, a=30, d = wall+2) {
    translate([0,0,-1]) rotate([0,0,-a]) union() {
        cube([dia, w/cos(a), d]);
        translate([dia/2,0,0]) cylinder(d=dia, h = d);
        translate([dia/2,w/cos(a),0]) cylinder(d=dia, h = d);
    }
}

// electrocookie mini
module mount() {
    $fn = 10;
    cube(proto);
    
    // m2 corner posts
    translate([3.15, 3.15, 0]) post(6, m2, 5, 3); 
    translate([proto.x - 3.15, 3.15, 0]) post(6, m2, 5, 3); 
    translate([3.15, proto.y - 3.15, 0]) post(6, m2, 5, 3); 
    translate([proto.x - 3.15, proto.y - 3.15, 0]) post(6, m2, 5, 3); 
    
    // m3 center posts
    translate([5.1, proto.y / 2, 0]) post(6, m3, 5, 3); 
    translate([proto.x - 5.1, proto.y / 2, 0]) post(6, m3, 5, 3); 
}


module post(od, id, height, depth) {
    $fn = 32;
    difference() {
        cylinder(d=od, h=height);
        translate([0, 0, height - depth]) cylinder(d=id, h=depth+0.01);
    }
}

module rcube(dims_or_size, r, center = false) {
    $fn = 16;
    dims = (dims_or_size[0] == undef) ? [dims_or_size, dims_or_size, dims_or_size] : dims_or_size;
    translate(center ? [-dims.x/2, -dims.y/2, -dims.z/2] : [0,0,0]) 
    union() {
        // cubes in x,y,z directions for the middle
        translate([r,r,0]) cube([dims.x - 2*r, dims.y - 2*r, dims.z]); 
        translate([r,0,r]) cube([dims.x - 2*r, dims.y, dims.z - 2*r]);
        translate([0,r,r]) cube([dims.x, dims.y - 2*r, dims.z - 2*r]);
        
        // cylinders for the vertices
        translate([r,r,r]) cylinder(dims.z - 2*r, r = r);
        translate([dims.x-r,r,r]) cylinder(dims.z - 2*r, r = r);
        translate([r,dims.y-r,r]) cylinder(dims.z - 2*r, r = r);
        translate([dims.x-r,dims.y-r,r]) cylinder(dims.z - 2*r, r = r);
        
        translate([r,r,r]) rotate([-90,0,0]) cylinder(dims.y - 2*r, r = r);
        translate([dims.x-r,r,r]) rotate([-90,0,0]) cylinder(dims.y - 2*r, r = r);
        translate([r,r,dims.z-r]) rotate([-90,0,0]) cylinder(dims.y - 2*r, r = r);
        translate([dims.x-r,r,dims.z-r]) rotate([-90,0,0]) cylinder(dims.y - 2*r, r = r);
        
        translate([r,r,r]) rotate([0,90,0]) cylinder(dims.x - 2*r, r = r);
        translate([r,r,dims.z-r]) rotate([0,90,0]) cylinder(dims.x - 2*r, r = r);
        translate([r,dims.y-r,r]) rotate([0,90,0]) cylinder(dims.x - 2*r, r = r);
        translate([r,dims.y-r,dims.z-r]) rotate([0,90,0]) cylinder(dims.x - 2*r, r = r);

        // spheres for the corners
        for (x = [r, dims.x - r]) {
            for (y = [r, dims.y - r]) {
                for (z = [r, dims.z - r]) {
                    translate([x,y,z]) sphere(r);
                }
            }
        }
    }
}
