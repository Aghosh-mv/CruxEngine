#!/usr/bin/env python3
"""
FROST ENGINE SCRIPT TRANSLATOR
Converts plain English game scripts to C++
"""

import re
import sys

class FrostTranslator:
    def __init__(self):
        self.entities = []
        self.materials = []
        self.lights = []
        self.scripts = []
        self.audio = []
        self.ui = []
        self.config = {}
        
    def parse(self, text):
        lines = text.split('\n')
        for line in lines:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            self.process_line(line)
            
    def process_line(self, line):
        # GAME SETUP
        if 'game name:' in line:
            match = re.search(r'game name:\s*"(.+?)"', line)
            if match: self.config['gameName'] = match.group(1)
            
        elif 'screen size:' in line:
            match = re.search(r'screen size:\s*(\d+)\s*x\s*(\d+)', line)
            if match:
                self.config['width'] = int(match.group(1))
                self.config['height'] = int(match.group(2))
                
        elif 'background color:' in line:
            self.config['bgColor'] = self.extract_color(line)
            
        # ENTITIES
        elif 'is a box' in line:
            self.parse_box(line)
        elif 'is a light of type' in line:
            self.parse_light(line)
        elif 'is a sphere' in line:
            self.parse_sphere(line)
            
        # PHYSICS
        elif 'enable physics' in line:
            match = re.search(r'gravity\s*([-\d.]+)', line)
            if match:
                self.scripts.append(f'physics.setGravity(0, {match.group(1)}, 0);')
                
        elif 'has rigidbody' in line:
            entity = self.extract_entity_name(line)
            mass = re.search(r'mass\s*(\d+)', line)
            self.scripts.append(f'{entity}.addComponent<RigidBody>();')
            if mass:
                self.scripts.append(f'{entity}.getComponent<RigidBody>().setMass({mass.group(1)});')
                
        elif 'is static collider' in line:
            entity = self.extract_entity_name(line)
            self.scripts.append(f'{entity}.addComponent<BoxCollider>();')
            
        # INPUT
        elif 'key "' in line:
            match = re.search(r'key "(.)":\s*(.+)', line)
            if match:
                key, action = match.groups()
                self.scripts.append(f'Input.bindKey({ord(key.upper())}, []() {{ {action} }});')
                
        # CAMERA
        elif 'camera: follows' in line:
            self.parse_camera(line)
            
        # GAME LOGIC
        elif 'When' in line:
            self.parse_event(line)
            
        # UI
        elif 'show' in line:
            self.parse_ui(line)
            
        # SOUND
        elif 'play music' in line:
            match = re.search(r'play music "(.+?)"', line)
            if match:
                self.audio.append(f'Audio.playMusic("{match.group(1)}");')
                
        elif 'when' in line and 'play sound' in line:
            self.parse_sound(line)
            
    def parse_box(self, line):
        name = self.extract_entity_name(line)
        pos = re.search(r'position\s*\(([^)]+)\)', line)
        size = re.search(r'size\s*\(([^)]+)\)', line)
        color = self.extract_color(line)
        
        props = f'name: "{name}"'
        if pos: props += f',\n  position: {pos.group(1)}'
        if size: props += f',\n  size: {size.group(1)}'
        if color: props += f',\n  color: {color}'
        
        self.entities.append(f'''
Entity {name} = scene.createEntity();
{name}.addComponent<Transform>({pos.group(1) if pos else "0, 0, 0"});
{name}.addComponent<Mesh>(MeshType::Box);
{name}.addComponent<Material>({color if color else "white"});
''')
        
    def parse_light(self, line):
        name = self.extract_entity_name(line)
        ltype = re.search(r'type\s+(\w+)', line)
        intensity = re.search(r'intensity\s*([\d.]+)', line)
        
        self.lights.append(f'''
Entity {name} = scene.createEntity();
{name}.addComponent<Light>(LightType::{ltype.group(1).title()});
''')
        
    def extract_entity_name(self, line):
        # Handle "enemy type goblin:" or "player:" etc
        match = re.search(r'(\w+(?:\s+\w+)?):\s+is', line)
        if match:
            name = match.group(1).replace(' ', '_').lower()
            return name
        match = re.search(r'^(\w+):', line)
        return match.group(1) if match else "entity"
        
    def extract_color(self, line):
        colors = {
            'red': 'Color(1,0,0)', 'green': 'Color(0,1,0)', 'blue': 'Color(0,0,1)',
            'white': 'Color(1,1,1)', 'black': 'Color(0,0,0)', 'yellow': 'Color(1,1,0)',
            'purple': 'Color(0.5,0,1)', 'sky blue': 'Color(0.5,0.8,1)'
        }
        for name, val in colors.items():
            if name in line.lower():
                return val
        return None
        
    def parse_event(self, line):
        # "When player touches goblin..."
        if 'touches' in line or 'hits' in line:
            match = re.search(r'When\s+(\w+)\s+touches?\s+(\w+)', line)
            if match:
                self.scripts.append(f'''
Event.onCollision({match.group(1)}, {match.group(2)}, []() {{
    // Handle collision
}});
''')
                
        elif 'health is less than 0' in line:
            self.scripts.append('''
Event.onDeath(entity, []() {
    destroy(entity);
});
''')
            
    def parse_ui(self, line):
        if 'show score' in line:
            self.ui.append('UI.addText("score", 10, 10, "Score: " + score);')
        elif 'show health' in line:
            self.ui.append('UI.addHealthBar(entity);')
            
    def parse_sound(self, line):
        # "when player jumps, play sound"
        match = re.search(r'when\s+(.+?),?\s*play sound "(.+?)"', line)
        if match:
            self.audio.append(f'Audio.playSound("{match.group(2)}"); // triggered by {match.group(1)}')
            
    def parse_camera(self, line):
        target = re.search(r'follows?\s+(\w+)', line)
        if target:
            self.scripts.append(f'''
Camera.follow({target.group(1)});
''')
            
    def translate(self) -> str:
        return f'''
// GENERATED BY FROST ENGINE TRANSLATOR
// Game: {self.config.get('gameName', 'Untitled')}

#include "FrostEngine/FrostEngine.h"

using namespace Frost;

struct Game : public Frost::Engine {{
    void init() override {{
        super.init();
        
        // Set up window
        setWindowSize({self.config.get('width', 1280)}, {self.config.get('height', 720)});
        
        // Create scene
        {self.generate_scene()}
        
        // Set up input
        {self.generate_input()}
        
        // Set up physics
        {self.generate_physics()}
        
        // Set up audio
        {self.generate_audio()}
        
        // Set up UI
        {self.generate_ui()}
    }}
    
    void update(f32 dt) override {{
        // Update game logic
        {self.generate_update()}
    }}
    
    void render() override {{
        // Render scene
    }}
    
    {self.generate_entities()}
    {self.generate_scripts()}
}};

int main(int argc, char** argv) {{
    Game game;
    game.init();
    game.run();
    return 0;
}}
'''
        
    def generate_scene(self):
        return f'scene.setBackgroundColor({self.config.get("bgColor", "Color(0.5, 0.8, 1)")});'
        
    def generate_entities(self):
        return '\n'.join(self.entities)
        
    def generate_input(self):
        return '\n'.join(self.scripts[:5])  # First few input bindings
        
    def generate_physics(self):
        gravity = re.search(r'gravity\s*([-\d.]+)', str(self.scripts))
        if gravity:
            return f'physics.setGravity(0, {gravity.group(1)}, 0);'
        return ''
        
    def generate_audio(self):
        return '\n'.join(self.audio)
        
    def generate_ui(self):
        return '\n'.join(self.ui)
        
    def generate_scripts(self):
        return '\n'.join(self.scripts)


def generate_update(self):
        return '\n'.join(self.scripts[5:])


def main():
    if len(sys.argv) < 2:
        print("Usage: frostc <game.frost>")
        return 1
        
    with open(sys.argv[1], 'r') as f:
        source = f.read()
        
    translator = FrostTranslator()
    translator.parse(source)
    
    output = translator.translate()
    
    out_file = sys.argv[1].replace('.frost', '.cpp')
    with open(out_file, 'w') as f:
        f.write(output)
        
    print(f"Translated {sys.argv[1]} -> {out_file}")
    print("Now run: g++ -o game game.cpp -lfrost")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())