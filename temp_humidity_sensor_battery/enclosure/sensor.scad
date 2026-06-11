inner_dims = [58, 38];
sensor = [16.4,13];

wall = 1.6;
tabd = 3;
m3 = 2.76;
cord=3.4;


$fn=32;



top();
translate([0, inner_dims.y+wall*2+10,0]) bottom(12);

module top() {
    h=16;
    difference() {
        union() {
            case(h);
            
            // walls to connect to base
            translate([wall+inner_dims.x*0.01,wall,0]) cube([inner_dims.x*.98,wall,wall+h+3]);
            translate([wall+inner_dims.x*0.01,inner_dims.y,0]) cube([inner_dims.x*.98,wall,wall+h+3]);
            
            // snap cyinders
            translate([inner_dims.x*.12+wall, wall+tabd/4+0.15, h+tabd]) rotate([0,90,0]) cylinder(h=inner_dims.x*.76, d=tabd);
            translate([inner_dims.x*.12+wall, wall+inner_dims.y-tabd/4-0.15, h+tabd]) rotate([0,90,0]) cylinder(h=inner_dims.x*.76, d=tabd);
        }
        
        // cut the cylinders for friction fit
        translate([wall, 2*wall+0.01, wall]) cube([inner_dims.x, inner_dims.y-2*wall-0.02, h*2]);
        
        // cut the sensor hole
        translate([wall+2,wall+inner_dims.y/2-sensor.y/2,-1]) cube([sensor.x, sensor.y, h]);

        //cut the cord hole
        translate([inner_dims.x,wall+inner_dims.y/2,h+wall-cord]) rotate([0,90,0]) cylinder(d=cord,h=wall*4);
        translate([inner_dims.x,wall+inner_dims.y/2-cord/2,h+wall]) rotate([0,90,0]) cube([cord,cord,wall*4]);
        
    }
    
    // screw post for sensor
    translate([wall+2+sensor.x+4.55,wall+inner_dims.y/2,wall]) post(5, m3, 3, 2.8);
}


module bottom(height = 12) {
    h = height+wall;
    difference() {
        case(h);
        
        // snap fit grooves
        translate([inner_dims.x*.1+wall, wall+tabd/4, h-tabd]) rotate([0,90,0]) cylinder(h=inner_dims.x*.8, d=tabd);
        translate([inner_dims.x*.1+wall, wall+inner_dims.y-tabd/4, h-tabd]) rotate([0,90,0]) cylinder(h=inner_dims.x*.8, d=tabd);
        
        // shaving wall for tolerence 
        translate([wall+inner_dims.x*0.05,inner_dims.y+0.2,wall]) cube([inner_dims.x*.9+wall,wall,wall+h+3]);
        translate([wall+inner_dims.x*0.05,wall-0.2,wall]) cube([inner_dims.x*.9+wall,wall,wall+h+3]);

    }

}



module case(height, male=true) {
    union() {
        difference() {
            rcube([inner_dims.x + 2*wall, inner_dims.y + 2*wall, height+5], 2);
            translate([-0.01, -0.01, height]) cube([inner_dims.x + 2*wall+1, inner_dims.y + 2*wall+1, height]);
            translate([wall, wall, wall]) cube([inner_dims.x, inner_dims.y, height]);
        }
    }
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