interface MetaBarProps {
    text: string;
}

export default function MetaBar({ text }: MetaBarProps) {
    return <section className="meta">{text}</section>;
}
