import java.nio.file.*;
import java.util.*;

/** 开发期调用原版注册表读取生成表，不实现原版群系或刷怪算法。 */
public class DrownedRulesOracle extends LegacyBiomeOracleSupport {
    public static void main(String[] args) throws Exception {
        call(cls("net.minecraft.SharedConstants"),"tryDetectVersion");
        call(cls("net.minecraft.server.Bootstrap"),"bootStrap");
        lookup = args[0].equals("1.19.3");
        if (lookup)
            registry = call(cls("net.minecraft.data.registries.VanillaRegistries"),"createLookup");
        Object biomes = registry("BIOME","BIOME");
        var lines = new ArrayList<String>();
        Object category = field(cls("net.minecraft.world.entity.MobCategory"),"MONSTER");
        Object drowned = field(cls("net.minecraft.world.entity.EntityType"),"DROWNED");
        for (String biome : List.of("river","dripstone_caves")) {
            Object value;
            if (lookup) {
                Object key = field(cls("net.minecraft.world.level.biome.Biomes"),biome.toUpperCase(Locale.ROOT));
                value = call(call(biomes,"getOrThrow",key),"value");
            } else {
                value = call(biomes,"get",create("net.minecraft.resources.ResourceLocation","minecraft:"+biome));
            }
            Object settings = call(value,"getMobSettings");
            var entries = (List<?>)call(call(settings,"getMobs",category),"unwrap");
            int total = 0, weight = 0, min = 0, max = 0;
            for (Object entry : entries) {
                int w = (Integer)call(call(entry,"getWeight"),"asInt");
                total += w;
                if (field(entry,"type") == drowned) {
                    weight = w;
                    min = (Integer)field(entry,"minCount");
                    max = (Integer)field(entry,"maxCount");
                }
            }
            lines.add(biome+" "+total+" "+weight+" "+min+" "+max);
        }
        Files.write(Path.of(args[1]),lines);
    }
}
