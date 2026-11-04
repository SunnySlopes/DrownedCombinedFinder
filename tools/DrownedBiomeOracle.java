import java.nio.file.*;
import java.lang.reflect.*;
import java.util.*;

/** 1.21.5至26.2开发期群系查询，只桥接原版注册表、采样器和BiomeManager。 */
public class DrownedBiomeOracle extends LegacyBiomeOracleSupport {
    public static void main(String[] args) throws Exception {
        call(cls("net.minecraft.SharedConstants"),"tryDetectVersion");
        call(cls("net.minecraft.server.Bootstrap"),"bootStrap");
        lookup = true;
        registry = call(cls("net.minecraft.data.registries.VanillaRegistries"),"createLookup");
        Object noises = registry("NOISE","NOISE");
        Object settingsRegistry = registry("NOISE_GENERATOR_SETTINGS","NOISE_SETTINGS");
        Object settings = call(call(settingsRegistry,"getOrThrow",
            field(cls("net.minecraft.world.level.levelgen.NoiseGeneratorSettings"),"OVERWORLD")),"value");
        Object parameters = registry("MULTI_NOISE_BIOME_SOURCE_PARAMETER_LIST","MULTI_NOISE_BIOME_SOURCE_PARAMETER_LIST");
        Object holder = call(parameters,"getOrThrow",
            field(cls("net.minecraft.world.level.biome.MultiNoiseBiomeSourceParameterLists"),"OVERWORLD"));
        Object source = call(cls("net.minecraft.world.level.biome.MultiNoiseBiomeSource"),"createFromPreset",holder);
        Object manager = null;
        long previous = 0;
        var output = new ArrayList<String>();
        for (String line : Files.readAllLines(Path.of(args[0]))) {
            String[] p = line.split(" ");
            long seed = Long.parseLong(p[0]);
            int x = Integer.parseInt(p[1]),y = Integer.parseInt(p[2]),z = Integer.parseInt(p[3]);
            if (manager == null || seed != previous) {
                Object state = call(cls("net.minecraft.world.level.levelgen.RandomState"),"create",settings,noises,seed);
                Object sampler = call(state,"sampler");
                Object resolver = Proxy.newProxyInstance(DrownedBiomeOracle.class.getClassLoader(),
                    new Class[]{cls("net.minecraft.world.level.biome.BiomeManager$NoiseBiomeSource")},
                    (proxy,method,values) -> call(source,"getNoiseBiome",values[0],values[1],values[2],sampler));
                manager = create("net.minecraft.world.level.biome.BiomeManager",resolver,
                    call(cls("net.minecraft.world.level.biome.BiomeManager"),"obfuscateSeed",seed));
                previous = seed;
            }
            Object biome = call(manager,"getBiome",create("net.minecraft.core.BlockPos",x,y,z));
            Object key = ((Optional<?>)call(biome,"unwrapKey")).orElseThrow();
            Object location;
            try { location = call(key,"location"); }
            catch (NoSuchMethodException ignored) { location = call(key,"identifier"); }
            output.add(location.toString().replace("minecraft:",""));
        }
        Files.write(Path.of(args[1]),output);
    }
}
